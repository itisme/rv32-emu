/*
 * tensix_impl.c - Tensix coprocessor implementation
 *
 * This file implements the main interface functions for the Tensix coprocessor.
 * It integrates the FIFO-based instruction dispatch with the instruction execution engine.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Include io.h from rv32emu/src (before tensix.h) to get memory_t definition
 * Note: -Irv32emu/src is before -Itt_cop, so "io.h" resolves to rv32emu/src/io.h
 */
#include "io.h"

#include "tensix.h"
#include "tensix_cop.h"
#include "mailbox.h"

/* Default log level: only errors and warnings */
int tt_log_level = TT_LOG_WARN;

/* Initialize tensix coprocessor */

void tensix_init(tensix_t *tt,
                 uint8_t *l1_scratchpad_mem,
                 uint8_t *high_mem,
                 uint8_t *t0_ldm,
                 uint8_t *t1_ldm,
                 uint8_t *t2_ldm,
                 uint16_t noc_xy)
{
    if (!tt) {
        TT_ERR("Error: tensix_init called with NULL pointer\n");
        return;
    }

    /* Clear tensix state */
    memset(tt, 0, sizeof(tensix_t));

    tt->noc_xy = noc_xy;

    /* Store memory pointers */
    tt->mem.l1_scratchpad = l1_scratchpad_mem;
    tt->mem.high_mem = high_mem;
    tt->mem.t0_ldm = t0_ldm;
    tt->mem.t1_ldm = t1_ldm;
    tt->mem.t2_ldm = t2_ldm;

    /* Initialize address mode */
    for (int i = 0; i < 8; i++) {
        tt->mutex[i] = MUTEX_NONE;
        tt->sem[i] = 0;
        tt->sem_max[i] = 0;
    }
    /* Default semaphore max values matching hardware/LLK boot.h defaults.
     * These can be overridden by TTI_SEMINIT in firmware.
     * sem[2] = UNPACK_TO_DEST: max=1 (T0 posts after unpack, T1 gets before math)
     * sem[4] = PACK_DONE:      max=1
     * sem[7] = MATH_DONE:      max=1 (T1 posts after math, T1 gets after pack signals) */
    tt->sem_max[2] = 1;  /* UNPACK_TO_DEST */
    tt->sem_max[4] = 1;  /* PACK_DONE */
    tt->sem_max[7] = 1;  /* MATH_DONE */

    /* Initialize thread registers */
    for (int t = 0; t < 3; t++) {
        for (int r = 0; r < THD_REG_COUNT; r++) {
            tt->thd_reg[t][r] = 0;
        }
        tt->cfg_state_id[t] = 0;
    }

    /* Initialize config registers */
    for (int c = 0; c < 2; c++) {
        for (int r = 0; r < CFG_REG_COUNT; r++) {
            tt->cfg_reg[c][r] = 0;
        }
    }

    /* Initialize DMA registers */
    for (int t = 0; t < 3; t++) {
        for (int r = 0; r < DMA_REG_COUNT; r++) {
            tt->dma_reg[t][r] = 0;
        }
    }

    /* Initialize pack configs */
    for (int i = 0; i < 4; i++) {
        memset(&tt->pack_configs[i], 0, sizeof(PackConfig));
    }

    /* Initialize address controllers */
    for (int i = 0; i < 3; i++) {
        memset(&tt->adc[i], 0, sizeof(AddrCtrl));
        for (int j = 0; j < 8; j++) {
            memset(&tt->adc[i].cntx[j], 0, sizeof(CfgCntx));
        }
    }

    /* Initialize SRCA, SRCB, DEST registers to zero */
    memset(tt->srca, 0, sizeof(tt->srca));
    memset(tt->srcb, 0, sizeof(tt->srcb));
    memset(tt->dest, 0, sizeof(tt->dest));

    tt->srca_dvalid[0] = tt->srca_dvalid[1] = false;
    tt->srcb_dvalid[0] = tt->srcb_dvalid[1] = false;
    tt->unp_srca_bank = 0;
    tt->unp_srcb_bank = 0;
    tt->math_srca_bank = 0;
    tt->math_srcb_bank = 0;
    tt->dest_dvalid = false;
    tt->pack_l1_write_offset = 0;
    tt->pack_l1_dest_addr_raw = 0;

    /* Initialize SFPU lane predication:
     * UseLaneFlagsForLaneEnable = true, LaneFlags = true → all lanes enabled.
     * This matches hardware reset state: LaneEnabled = !UseLaneFlags || LaneFlags = true.
     */
    tt->flag_stack_top = -1;  /* empty stack */
    for (int i = 0; i < LREG_LANES; i++) {
        tt->lane_flags[i] = true;
        tt->use_lane_flags[i] = true;
    }

    /* Initialize SFPU LReg constants */
    {
        float f_0_8373 = 0.8373f;
        uint32_t fp_0_8373;
        memcpy(&fp_0_8373, &f_0_8373, 4);
        uint32_t fp_1_0 = 0x3F800000; /* 1.0f */

        uint32_t fp_neg_1_0 = 0xBF800000; /* -1.0f (SFPI compiler reserves L11 for this) */

        for (int i = 0; i < LREG_LANES; i++) {
            tt->lreg[8][i] = fp_0_8373;
            tt->lreg[9][i] = 0;
            tt->lreg[10][i] = fp_1_0;
            tt->lreg[11][i] = fp_neg_1_0;
            tt->lreg[15][i] = 2 * i;
        }
    }

    /* IEEE 754 negative infinity */
    {
        uint32_t neg_inf_bits = 0xFF800000;
        memcpy(&tt->neginf, &neg_inf_bits, sizeof(float));
    }

    /* Allocate and initialize coprocessor state machine */
    if (!tt->cop) {
        tt->cop = (tensix_cop_t *)malloc(sizeof(tensix_cop_t));
        if (!tt->cop) {
            TT_ERR("Error: Failed to allocate tensix coprocessor state\n");
            return;
        }
    }
    tensix_cop_init(tt->cop, tt);
}

/* Push instruction to tensix coprocessor.
 * All instructions are pushed to FIFO and executed by tensix_step.
 * External callers expect: 1 push = 1 step execution.
 */
bool tensix_push(tensix_t *tt, uint32_t insn, int core_id)
{
    if (!tt || !tt->cop) {
        TT_ERR("Error: tensix_push called with invalid tensix instance\n");
        return false;
    }

    if (core_id < 0 || core_id > 2) {
        TT_ERR("Error: Invalid core_id %d in tensix_push\n", core_id);
        return false;
    }

    /* All instructions (including MOP and MOP_CFG) go into the FIFO */
    bool success = tensix_cop_push(tt->cop, core_id, insn);

    if (!success) {
        /* FIFO full - in real hardware this would stall the RISC-V core */
        TT_WARN("Warning: Tensix core %d FIFO full, instruction dropped\n", core_id);
    }
    return success;
}

/* Write to MOP_CFG register array (0xFFB80000 - 0xFFB80023) */
void tensix_write_mop_cfg(tensix_t *tt, int core_id, uint32_t offset, uint32_t data)
{
    if (!tt || !tt->cop || core_id < 0 || core_id > 2)
        return;

    uint32_t reg_idx = offset / 4;
    if (reg_idx < 9) {
        tt->cop->threads[core_id].mop_cfg[reg_idx] = data;
        TT_DBG("[MOP_CFG] core=%d, mop_cfg[%d] = 0x%x\n", core_id, reg_idx, data);
    }
}

/* Run one cycle of tensix coprocessor for a specific core (FIFO instructions) */
bool tensix_step(tensix_t *tt, int core_id)
{
    if (!tt || !tt->cop) {
        return false;
    }

    /* Execute one instruction for the specified core only */
    return tensix_cop_step(tt->cop, core_id);
}

/* Set direct instruction (for inline TT instructions in code) */
void tensix_set_direct(tensix_t *tt, uint32_t insn, int core_id)
{
    if (!tt || !tt->cop || core_id < 0 || core_id > 2) {
        return;
    }
    tensix_cop_set_direct(tt->cop, core_id, insn);
}

/* Execute direct instruction (supports blocking on Wait Gate) */
bool tensix_step_direct(tensix_t *tt, int core_id)
{
    if (!tt || !tt->cop) {
        return false;
    }
    return tensix_cop_step_direct(tt->cop, core_id);
}

/* Run one cycle for all cores */
void tensix_step_all(tensix_t *tt)
{
    if (!tt || !tt->cop) {
        return;
    }

    /* Execute all three cores */
    tensix_cop_step_all(tt->cop);
}

/* Reset tensix coprocessor state */
void tensix_reset(tensix_t *tt)
{
    if (!tt || !tt->cop) {
        return;
    }

    /* Reset coprocessor state machine */
    tensix_cop_reset(tt->cop);

    /* Reset core state */
    memset(tt->srca, 0, sizeof(tt->srca));
    memset(tt->srcb, 0, sizeof(tt->srcb));
    memset(tt->dest, 0, sizeof(tt->dest));
    tt->srca_dvalid[0] = tt->srca_dvalid[1] = false;
    tt->srcb_dvalid[0] = tt->srcb_dvalid[1] = false;
    tt->unp_srca_bank = 0;
    tt->unp_srcb_bank = 0;
    tt->math_srca_bank = 0;
    tt->math_srcb_bank = 0;
    tt->dest_dvalid = false;
    tt->pack_l1_write_offset = 0;
    tt->pack_l1_dest_addr_raw = 0;

    /* Reset mutexes and semaphores */
    for (int i = 0; i < 8; i++) {
        tt->mutex[i] = MUTEX_NONE;
        tt->sem[i] = 0;
    }
}

/* Get LDM pointer for a specific core */
static inline uint8_t *get_core_ldm(tensix_t *tt, int core_id)
{
    if (!tt) return NULL;
    switch (core_id) {
        case 0: return tt->mem.t0_ldm;
        case 1: return tt->mem.t1_ldm;
        case 2: return tt->mem.t2_ldm;
        default: return NULL;
    }
}

/* Check if address is per-core (needs LDM routing by tt_cop)
 * Note: 0xFFB00000-0xFFB01FFF (MEM_LOCAL_BASE) is handled by rv32emu's mem_local
 */
static inline bool is_per_core_addr(uint32_t addr)
{
    return (addr >= 0xFFB80000 && addr <= 0xFFB80023) ||  /* MOP_CFG (36B) */
           (addr >= 0xFFE00000 && addr <= 0xFFE00FFF) ||  /* REGFILE (4KB) */
           (addr >= 0xFFE80000 && addr <= 0xFFE8001F);    /* PC_BUF (32B) */
}

/* Translate per-core virtual address to LDM offset
 * LDM layout (16KB per core):
 *   0x0000-0x1FFF: MEM_LOCAL_BASE (handled by rv32emu mem_local)
 *   0x2000-0x20FF: MOP_CFG (0xFFB80000-0xFFB80023, padded to 256B)
 *   0x2100-0x30FF: REGFILE (0xFFE00000-0xFFE00FFF, 4KB)
 *   0x3100-0x31FF: PC_BUF (0xFFE80000-0xFFE8001F, padded to 256B)
 */
static inline uint32_t addr_to_ldm_offset(uint32_t addr)
{
    if (addr >= 0xFFB80000 && addr <= 0xFFB80023)
        return 0x2000 + (addr - 0xFFB80000);  /* MOP_CFG */
    if (addr >= 0xFFE00000 && addr <= 0xFFE00FFF)
        return 0x2100 + (addr - 0xFFE00000);  /* REGFILE: 4KB */
    if (addr >= 0xFFE80000 && addr <= 0xFFE8001F)
        return 0x3100 + (addr - 0xFFE80000);  /* PC_BUF */
    return 0xFFFFFFFF;
}

/* Generic MMIO read handler for all TT-specific addresses.
 * Returns true if handled (value written to *result, caller should NOT do normal memory read).
 * Returns false if not a special address (caller should do normal memory read).
 *
 * Design principle: All addresses handled by tensix_mmio_write() should also be handled here
 * to maintain read/write path symmetry.
 */
bool tensix_mmio_read(tensix_t *tt, int core_id, uint32_t addr, uint32_t *result)
{
    if (!tt || !result) {
        return false;
    }

    /* Mailbox read: 0xFFEC0000 - 0xFFEC3FFF */
    if (addr >= 0xFFEC0000 && addr <= 0xFFEC3FFF) {
        return mailbox_read(tt, core_id, addr, result);
    }

    /* Semaphore read: 0xFFE80020 - 0xFFE8003F (8 semaphores × 4 bytes)
     * Mapping: pc_buf_base[PC_BUF_SEMAPHORE_BASE + i] = 0xFFE80000 + (8+i)*4 = 0xFFE80020 + i*4
     * This is the CRITICAL fix: RISC-V firmware polls these addresses to read semaphore values.
     * Without this handler, reads go to uninitialized mem_high and always return 0.
     */
    if (addr >= 0xFFE80020 && addr <= 0xFFE8003F) {
        uint32_t sem_index = (addr - 0xFFE80020) / 4;
        if (sem_index < 8) {
            *result = tt->sem[sem_index];
            TT_DBG("[MMIO_READ] addr=0x%08x, core=%d, sem[%d]=%d\n",
                   addr, core_id, sem_index, *result);
            return true;
        }
    }

    /* Per-core addresses: route to independent LDM
     * - MOP_CFG: 0xFFB80000 - 0xFFB80023 (36B)
     * - REGFILE: 0xFFE00000 - 0xFFE00FFF (4KB)
     * - PC_BUF:  0xFFE80000 - 0xFFE8001F (32B)
     */
    if (is_per_core_addr(addr) && core_id >= 0 && core_id <= 2) {
        uint8_t *ldm = get_core_ldm(tt, core_id);
        uint32_t ldm_off = addr_to_ldm_offset(addr);
        if (ldm && ldm_off < 0x4000) {
            *result = *(uint32_t *)(ldm + ldm_off);
            /* Debug log for MOP_CFG reads */
            if (addr >= 0xFFB80000 && addr <= 0xFFB80023) {
                uint32_t reg_idx = (addr - 0xFFB80000) / 4;
                TT_DBG("[MMIO_READ] addr=0x%08x, core=%d, mop_cfg[%d]=0x%08x\n",
                       addr, core_id, reg_idx, *result);
            }
            return true;
        }
    }

    /* Instruction buffer reads: 0xFFE40000 - 0xFFE6FFFF
     * NOTE: Instruction buffers are write-only FIFOs. Reads should not happen in normal operation.
     * If a read occurs, return 0 and log a warning.
     */
    if (addr >= 0xFFE40000 && addr <= 0xFFE6FFFF) {
        *result = 0;
        TT_WARN("[MMIO_READ_WARNING] addr=0x%08x, core=%d, reading from write-only instruction buffer\n",
               addr, core_id);
        return true;
    }

    /* Stream overlay registers: 0xFFB40000 - 0xFFB7FFFF
     * TODO: Implement if needed. Currently these are read from mem_high directly.
     * For now, return false to allow normal memory read.
     */
    if (addr >= 0xFFB40000 && addr < 0xFFB80000) {
        /* Fall through to normal memory read */
        return false;
    }

    /* Tensix config registers: 0xFFEF0000 - 0xFFEFFFFF
     * TODO: Implement if needed. Currently these are read from mem_high directly.
     * For now, return false to allow normal memory read.
     */
    if (addr >= 0xFFEF0000 && addr <= 0xFFEFFFFF) {
        /* Fall through to normal memory read */
        return false;
    }

    return false;  /* Not a special address, do normal memory read */
}

/* Generic MMIO write handler for all TT-specific addresses.
 * Returns true if handled (caller should NOT do normal memory write).
 * Returns false if not a special address (caller should do normal memory write).
 *
 * Design principle: All addresses handled here should also be handled by tensix_mmio_read()
 * to maintain read/write path symmetry.
 */
bool tensix_mmio_write(tensix_t *tt, int core_id, uint32_t addr, uint32_t data)
{
    /* Log ALL writes to high memory range for debugging */
    if (addr >= 0xFFB80000 && addr <= 0xFFB8FFFF) {
        TT_DBG("[MMIO_WRITE] addr=0x%08x, core=%d, data=0x%08x\n", addr, core_id, data);
    }

    /* MOP_CFG: log ALL writes regardless of core_id */
    if (addr >= 0xFFB80000 && addr <= 0xFFB80023) {
        uint32_t reg_idx = (addr - 0xFFB80000) / 4;
        TT_DBG("[MOP_CFG_WRITE] addr=0x%08x, core=%d, reg[%d]=0x%08x, opcode=0x%02x, is_per_core=%d\n",
               addr, core_id, reg_idx, data, (data >> 24) & 0xFF,
               is_per_core_addr(addr) && core_id >= 0 && core_id <= 2);
    }

    /* Mailbox write: 0xFFEC0000 - 0xFFEC3FFF
     * Push to FIFO but also let normal memory write proceed (return false),
     * because other code may read from mem_high directly. */
    if (addr >= 0xFFEC0000 && addr <= 0xFFEC3FFF) {
        mailbox_write(tt, core_id, addr, data);
        return false;  /* also write to mem_high */
    }

    /* Semaphore write: 0xFFE80020 - 0xFFE8003F (8 semaphores × 4 bytes)
     * RISC-V firmware uses semaphore_post(i) = write 0, semaphore_get(i) = write 1.
     * Route these to tt->sem[] so the coprocessor Wait Gate sees the changes.
     */
    if (addr >= 0xFFE80020 && addr <= 0xFFE8003F) {
        uint32_t sem_index = (addr - 0xFFE80020) / 4;
        if (sem_index < 8) {
            if (data == 0) {
                /* semaphore_post: increment (matching SEMPOST semantics) */
                if (tt->sem[sem_index] < 15) {
                    tt->sem[sem_index]++;
                }
            } else if (data == 1) {
                /* semaphore_get: decrement (matching SEMGET semantics) */
                if (tt->sem[sem_index] > 0) {
                    tt->sem[sem_index]--;
                }
            } else {
                /* Direct value write (e.g. initialization) */
                tt->sem[sem_index] = data & 0xF;
            }
            TT_DBG("[MMIO_WRITE] addr=0x%08x, core=%d, sem[%d]=%d (written=%d)\n",
                   addr, core_id, sem_index, tt->sem[sem_index], data);
            return true;  /* Handled: don't write to mem_high */
        }
    }

    /* Per-core addresses: route to independent LDM */
    if (is_per_core_addr(addr) && core_id >= 0 && core_id <= 2) {
        uint8_t *ldm = get_core_ldm(tt, core_id);
        uint32_t ldm_off = addr_to_ldm_offset(addr);
        if (ldm && ldm_off < 0x4000) {
            *(uint32_t *)(ldm + ldm_off) = data;
        }

        /* MOP_CFG also needs to update the mop_cfg array for MOP execution */
        if (addr >= 0xFFB80000 && addr <= 0xFFB80023) {
            tensix_write_mop_cfg(tt, core_id, addr - 0xFFB80000, data);
        }
        return true;
    }

    /* Instruction buffer pushes: 0xFFE40000 - 0xFFE6FFFF
     * Per PushTensixInstruction.md:
     * - INSTRN_BUF_BASE (0xFFE40000): Brisc→t0, Trisc0→t0, Trisc1→t1, Trisc2→t2
     * - INSTRN1_BUF_BASE (0xFFE50000): Brisc→t1, Trisc0/1/2 will hang
     * - INSTRN2_BUF_BASE (0xFFE60000): Brisc→t2, Trisc0/1/2 will hang
     * - NCrisc cannot push instructions
     */
    if (addr >= 0xFFE40000 && addr <= 0xFFE6FFFF) {
        if (!tt) return true;

        int target_thread = -1;

        if (addr <= 0xFFE4FFFF) {
            /* INSTRN_BUF_BASE */
            if (core_id >= 0 && core_id <= 2)
                target_thread = core_id;       /* Trisc: push to own thread */
            else if (core_id == -1)
                target_thread = 0;             /* Brisc: push to t0 */
            else
                TT_WARN("[MMIO_WRITE_ERROR] core_id=%d attempted to write INSTRN_BUF_BASE, ignoring\n", core_id);
        } else if (addr <= 0xFFE5FFFF) {
            if (core_id == -1)
                target_thread = 1;             /* Brisc: push to t1 */
            else if (core_id >= 0 && core_id <= 2)
                TT_WARN("[MMIO_WRITE_ERROR] core_id=%d (Trisc) attempted to write INSTRN1_BUF_BASE, would hang in real hardware\n", core_id);
        } else {
            if (core_id == -1)
                target_thread = 2;             /* Brisc: push to t2 */
            else if (core_id >= 0 && core_id <= 2)
                TT_WARN("[MMIO_WRITE_ERROR] core_id=%d (Trisc) attempted to write INSTRN2_BUF_BASE, would hang in real hardware\n", core_id);
        }

        if (target_thread >= 0) {
            /* Drain COP FIFO until there is room, then push.
             * The loop is bounded (FIFO_SIZE steps max) — safe in a coroutine.
             * If the COP is blocked (dvalid / WaitGate) and the FIFO is full,
             * that means firmware pushed more instructions than the COP can
             * buffer while waiting: this should never happen with correct
             * firmware + correct dvalid/WaitGate emulation. */
            while (!tensix_push(tt, data, target_thread)) {
                if (!tensix_step(tt, target_thread)) {
                    /* FIFO full AND COP blocked — firmware/emulation bug.
                     * We cannot spin-wait (coroutine constraint) and we cannot
                     * silently drop instructions.  Assert to enter debug state. */
                    tensix_thread_cop_t *thr = &tt->cop->threads[target_thread];
                    fprintf(stderr,
                        "[BUG] INSTRN_BUF: core=%d target=%d FIFO full+blocked"
                        " insn=0x%08x fifo_count=%d",
                        core_id, target_thread, data,
                        tensix_cop_fifo_count(tt->cop, target_thread));
                    if (thr->has_current_insn)
                        fprintf(stderr, " blocked_op=0x%02x",
                                (thr->current_insn >> 24) & 0xFF);
                    fprintf(stderr, "\n");
                    assert(0 && "INSTRN_BUF FIFO full and COP blocked");
                }
            }
        }
        return true;
    }

    /* Stream overlay registers: 0xFFB40000 - 0xFFB7FFFF
     * Normal memory write still needed, but UPDATE reg has side effect.
     */
    if (addr >= 0xFFB40000 && addr < 0xFFB80000) {
        uint32_t off_in_stream = (addr - 0xFFB40000) & 0xFFF;
        if (off_in_stream == 0x438) {
            /* UPDATE register (reg 270): extract 17-bit signed increment
             * from bits[22:6] and add to AVAILABLE register (reg 297).
             */
            uint32_t raw = (data >> 6) & 0x1FFFF;
            int32_t increment = (raw & 0x10000) ? (int32_t)(raw | 0xFFFE0000) : (int32_t)raw;
            uint32_t avail_addr = (addr - 0xFFB00000) + 0x6C; /* +0x6C = 0x4A4 - 0x438 */
            if (tt->mem.high_mem && avail_addr < 0x400000) {
                uint32_t old_val = *(uint32_t *)(tt->mem.high_mem + avail_addr);
                *(uint32_t *)(tt->mem.high_mem + avail_addr) += (uint32_t)increment;
                TT_DBG("tensix_mmio: UPDATE @0x%x, core=%d, data=0x%x, incr=%d, avail %u -> %u\n",
                       addr, core_id, data, increment, old_val, old_val + (uint32_t)increment);
            }
        }
        return false; /* still do normal memory write */
    }

    /* Tensix config registers: 0xFFEF0000 - 0xFFEFFFFF
     * These are written directly to high_mem by TRISC.
     * UNPACR/PACR read from high_mem via tensix_read_cfg().
     * Debug print for key registers (TileDescriptor, base_addr, offset).
     */
    if (addr >= 0xFFEF0000 && addr <= 0xFFEFFFFF) {
        uint32_t reg_idx = (addr - 0xFFEF0000) / 4;
        if (reg_idx == 72 || reg_idx == 76 || reg_idx == 77)
            fprintf(stderr, "[CFG:MMIO_WRITE] noc=0x%x core=%d cfg[%u]=0x%x\n",
                    tt ? tt->noc_xy : 0, core_id, reg_idx, data);
        if (reg_idx == 76 || reg_idx == 92 || reg_idx == 124 || reg_idx == 140 ||
            (reg_idx >= 64 && reg_idx <= 67) || (reg_idx >= 112 && reg_idx <= 115)) {
            TT_DBG("[CFG_MMIO] core=%d, cfg[%d] = 0x%x (addr=0x%x)\n",
                   core_id, reg_idx, data, addr);
        }
        /* Reset pack L1 write offset when L1_Dest_addr (cfg[69]) is written */
        if (reg_idx == 69) {
            tt->pack_l1_write_offset = 0;
            fprintf(stderr, "[DBG:MMIO:CFG69] noc=0x%x reg_idx=69 val=0x%08x\n",
                    tt->noc_xy, data);
        }
        /* PRNG seed: cfg[186] (PRNG_SEED_Seed_Val_ADDR32) or cfg[186+224] (bank 1) */
        if (reg_idx == 186 || reg_idx == (186 + 224)) {
            for (int lane = 0; lane < 32; lane++)
                tt->prng_state[lane] = data + lane;
        }
        return false; /* normal memory write to high_mem */
    }

    return false;
}

/* ============================================================================
 * tensix_clear: reset per-kernel state when all cores return to firmware.
 * Called from emulate.c when the last core exits kernel space (PC < 0xa000).
 * ============================================================================ */
void tensix_clear(tensix_t *tt)
{
    if (!tt)
        return;

    {
        uint32_t c72 = tensix_read_cfg(&tt->mem, 72);
        uint32_t c76 = tensix_read_cfg(&tt->mem, 76);
        uint32_t c77 = tensix_read_cfg(&tt->mem, 77);
        fprintf(stderr, "[TENSIX_CLEAR] resetting per-kernel state cfg[72]=0x%x cfg[76]=0x%x cfg[77]=0x%x\n",
                c72, c76, c77);
    }

    /* Save fields that must survive the wipe */
    tensix_memory_t saved_mem    = tt->mem;
    tensix_cop_t   *saved_cop    = tt->cop;
    uint16_t        saved_noc_xy = tt->noc_xy;
    uint8_t         saved_cores  = tt->cores_in_kernel;
    /* sem_max is written by firmware at device init and not re-written
     * before each kernel; preserve it across clears. */
    uint32_t saved_sem_max[8];
    for (int i = 0; i < 8; i++) saved_sem_max[i] = tt->sem_max[i];

    /* Wipe all runtime state (mop_templ_0/1 contain only unused TemplateOp
     * function pointer fields — verified no .c code references them) */
    memset(tt, 0, sizeof(*tt));

    /* Restore structural / persistent fields */
    tt->mem             = saved_mem;
    tt->cop             = saved_cop;
    tt->noc_xy          = saved_noc_xy;
    tt->cores_in_kernel = saved_cores;
    for (int i = 0; i < 8; i++) tt->sem_max[i] = saved_sem_max[i];

    /* Restore non-zero initial values */
    for (int i = 0; i < 8; i++)
        tt->mutex[i] = MUTEX_NONE;  /* 0xFF */
    tt->flag_stack_top = -1;
    tt->neginf = -__builtin_inff();

    /* Reset entire COP struct, preserving structural/callback fields */
    if (saved_cop) {
        tensix_t   *saved_core       = saved_cop->core;
        void       *saved_mem_ctx    = saved_cop->mem_ctx;
        uint32_t  (*saved_mem_read )(void *, uint32_t, uint32_t)
                                     = saved_cop->mem_read_fn;
        void      (*saved_mem_write)(void *, uint32_t, uint32_t, uint32_t)
                                     = saved_cop->mem_write_fn;

        /* mop_cfg and zmask_hi16 are hardware registers that persist across
         * kernel dispatches on real hardware — preserve them across clears. */
        uint32_t saved_mop_cfg[3][9];
        uint32_t saved_zmask_hi16[3];
        for (int t = 0; t < 3; t++) {
            for (int i = 0; i < 9; i++)
                saved_mop_cfg[t][i] = saved_cop->threads[t].mop_cfg[i];
            saved_zmask_hi16[t] = saved_cop->threads[t].zmask_hi16;
        }

        memset(saved_cop, 0, sizeof(*saved_cop));

        saved_cop->core         = saved_core;
        saved_cop->mem_ctx      = saved_mem_ctx;
        saved_cop->mem_read_fn  = saved_mem_read;
        saved_cop->mem_write_fn = saved_mem_write;
        for (int t = 0; t < 3; t++) {
            for (int i = 0; i < 9; i++)
                saved_cop->threads[t].mop_cfg[i] = saved_mop_cfg[t][i];
            saved_cop->threads[t].zmask_hi16 = saved_zmask_hi16[t];
        }
    }

    /* Clear high_mem (0xFFB00000-0xFFEFFFFF, 6MB), preserving CFG registers.
     * On real hardware, CFG registers persist across kernel dispatches —
     * firmware uses a one-time init guard and relies on CFG values surviving.
     * Stream overlay, mailbox backing store, and other MMIO state are reset. */
    if (saved_mem.high_mem) {
        uint32_t cfg_start = TENSIX_CFG_OFFSET_IN_HIGH_MEM;
        uint32_t cfg_size  = CFG_REG_COUNT * 4;
        uint32_t cfg_end   = cfg_start + cfg_size;
        memset(saved_mem.high_mem, 0, cfg_start);
        if (cfg_end < 0x600000)
            memset(saved_mem.high_mem + cfg_end, 0, 0x600000 - cfg_end);
    }

}

/* ============================================================================
 * Debug: dump tensix state at kernel entry (called from emulate.c).
 * Remove this function and its call site when debugging is done.
 * ============================================================================ */
void tensix_debug_dump_kernel_entry(tensix_t *tt)
{
    if (!tt || !tt->cop)
        return;

    tensix_cop_t *cop = tt->cop;
    fprintf(stderr, "\n=== [TENSIX_STATE] kernel entry ===\n");

    /* --- COP per-thread state --- */
    for (int tid = 0; tid < 3; tid++) {
        tensix_thread_cop_t *th = &cop->threads[tid];
        tensix_fifo_t *fifo = &th->insn_fifo;
        fprintf(stderr, "  T%d fifo=%u", tid, fifo->count);
        if (fifo->count) {
            fprintf(stderr, " [");
            for (uint32_t i = 0; i < fifo->count; i++) {
                uint32_t idx = (fifo->head + i) % TENSIX_FIFO_SIZE;
                fprintf(stderr, "%s0x%08x", i ? "," : "", fifo->buffer[idx]);
            }
            fprintf(stderr, "]");
        }
        if (th->has_current_insn)
            fprintf(stderr, " pending=0x%08x", th->current_insn);
        if (th->wait_gate.active)
            fprintf(stderr, " WAIT{op=0x%02x blk=0x%03x cond=0x%04x sem=0x%02x}",
                    th->wait_gate.opcode, th->wait_gate.block_mask,
                    th->wait_gate.condition_mask, th->wait_gate.semaphore_mask);
        fprintf(stderr, "\n       mop_cfg=[");
        for (int i = 0; i < 9; i++)
            fprintf(stderr, "%s0x%08x", i ? "," : "", th->mop_cfg[i]);
        fprintf(stderr, "] expand=%d zmask_hi=0x%x\n", th->mop_state.active, th->zmask_hi16);
    }

    /* --- Bank tracking --- */
    fprintf(stderr, "  banks: unp_srca=%u unp_srcb=%u math_srca=%u math_srcb=%u"
                    " last_unp_srca=%u mova2d_bank=%u(valid=%d)\n",
            tt->unp_srca_bank, tt->unp_srcb_bank,
            tt->math_srca_bank, tt->math_srcb_bank,
            tt->last_unp_srca_bank,
            tt->mova2d_latched_bank, tt->mova2d_bank_valid);

    /* --- dvalid + dest_offset --- */
    fprintf(stderr, "  dvalid: srca=[%d,%d] srcb=[%d,%d] srcb_zeroed=[%d,%d]"
                    " dest=%d dest_offset=%u\n",
            tt->srca_dvalid[0], tt->srca_dvalid[1],
            tt->srcb_dvalid[0], tt->srcb_dvalid[1],
            tt->srcb_zeroed[0], tt->srcb_zeroed[1],
            tt->dest_dvalid, tt->dest_offset);

    /* --- RWC counters + cfg_state_id + fidelity per thread --- */
    for (int tid = 0; tid < 3; tid++) {
        fprintf(stderr, "  T%d rwc: srca=%u srcb=%u dest=%u"
                        " | cr: srca=%u srcb=%u dest=%u"
                        " cfg_state_id=%u fidelity=%u\n",
                tid,
                tt->srca_rwc[tid], tt->srcb_rwc[tid], tt->dest_rwc[tid],
                tt->srca_rwc_cr[tid], tt->srcb_rwc_cr[tid], tt->dest_rwc_cr[tid],
                tt->cfg_state_id[tid], tt->fidelity[tid]);
    }

    /* --- thd_reg per thread (all, skip trailing zeros) --- */
    for (int tid = 0; tid < 3; tid++) {
        int last = -1;
        for (int i = THD_REG_COUNT - 1; i >= 0; i--) {
            if (tt->thd_reg[tid][i]) { last = i; break; }
        }
        if (last >= 0) {
            fprintf(stderr, "  T%d thd_reg[0..%d]=[", tid, last);
            for (int i = 0; i <= last; i++)
                fprintf(stderr, "%s0x%x", i ? "," : "", tt->thd_reg[tid][i]);
            fprintf(stderr, "]\n");
        } else {
            fprintf(stderr, "  T%d thd_reg=all_zero\n", tid);
        }
    }

    /* --- ADC position counters per channel --- */
    for (int tid = 0; tid < 3; tid++) {
        AddrCtrl *a = &tt->adc[tid];
        fprintf(stderr, "  T%d adc ch0=[x=%u y=%u z=%u w=%u]"
                        " cr=[x=%u y=%u z=%u w=%u]"
                        " ch1=[y=%u z=%u w=%u]\n",
                tid,
                a->ch0_x, a->ch0_y, a->ch0_z, a->ch0_w,
                a->ch0_x_cr, a->ch0_y_cr, a->ch0_z_cr, a->ch0_w_cr,
                a->ch1_y, a->ch1_z, a->ch1_w);
    }

    /* --- Semaphores + sem_math_pack + mutex --- */
    fprintf(stderr, "  sem=[");
    for (int i = 0; i < 8; i++)
        fprintf(stderr, "%s%u", i ? "," : "", tt->sem[i]);
    fprintf(stderr, "] sem_max=[");
    for (int i = 0; i < 8; i++)
        fprintf(stderr, "%s%u", i ? "," : "", tt->sem_max[i]);
    fprintf(stderr, "] sem_math_pack=%u bias=%u\n", tt->sem_math_pack, tt->bias);

    {
        bool any = false;
        for (int i = 0; i < 8; i++) if (tt->mutex[i]) { any = true; break; }
        if (any) {
            fprintf(stderr, "  mutex=[");
            for (int i = 0; i < 8; i++)
                fprintf(stderr, "%s%u", i ? "," : "", tt->mutex[i]);
            fprintf(stderr, "]\n");
        }
    }

    /* --- Pack state --- */
    fprintf(stderr, "  pack: l1_write_offset=%u l1_dest_addr_raw=0x%x\n",
            tt->pack_l1_write_offset, tt->pack_l1_dest_addr_raw);

    /* --- Mailbox counts + stall --- */
    {
        bool any = false;
        for (int s = 0; s < MAILBOX_CORES; s++)
            for (int d = 0; d < MAILBOX_CORES; d++)
                if (tt->mailbox_count[s][d]) { any = true; break; }
        for (int i = 0; i < MAILBOX_CORES && !any; i++)
            if (tt->mailbox_stall[i]) any = true;
        if (any) {
            for (int s = 0; s < MAILBOX_CORES; s++)
                for (int d = 0; d < MAILBOX_CORES; d++)
                    if (tt->mailbox_count[s][d])
                        fprintf(stderr, "  mailbox[%d->%d] count=%d\n",
                                s, d, tt->mailbox_count[s][d]);
            fprintf(stderr, "  mailbox_stall=[%d,%d,%d,%d]\n",
                    tt->mailbox_stall[0], tt->mailbox_stall[1],
                    tt->mailbox_stall[2], tt->mailbox_stall[3]);
        }
    }

    /* --- Replay state + first 8 buffer entries --- */
    for (int tid = 0; tid < 3; tid++) {
        fprintf(stderr, "  T%d replay: recording=%d expanding=%d"
                        " count=%u cur=%u idx=%u left=%u",
                tid,
                tt->replay_recording[tid], tt->replay_expanding[tid],
                tt->replay_expand_count[tid], tt->replay_expand_current[tid],
                tt->replay_index[tid], tt->replay_left[tid]);
        if (tt->replay_expand_count[tid]) {
            uint32_t show = tt->replay_expand_count[tid] < 8
                          ? tt->replay_expand_count[tid] : 8;
            fprintf(stderr, " buf=[");
            for (uint32_t i = 0; i < show; i++)
                fprintf(stderr, "%s0x%08x", i ? "," : "", tt->replay_buffer[tid][i]);
            if (tt->replay_expand_count[tid] > 8) fprintf(stderr, ",...");
            fprintf(stderr, "]");
        }
        fprintf(stderr, "\n");
    }

    /* --- cfg_reg: print non-zero entries (bank 0 and bank 1) --- */
    for (int bank = 0; bank < 2; bank++) {
        bool hdr = false;
        for (int i = 0; i < CFG_REG_COUNT; i++) {
            if (tt->cfg_reg[bank][i]) {
                if (!hdr) { fprintf(stderr, "  cfg_reg[bank%d]:", bank); hdr = true; }
                fprintf(stderr, " [%d]=0x%x", i, tt->cfg_reg[bank][i]);
            }
        }
        if (hdr) fprintf(stderr, "\n");
    }

    /* --- Binary snapshot: write entire tensix_t to file for external diff --- */
    {
        static int snap_count = 0;
        char path[64];
        snprintf(path, sizeof(path), "/tmp/tensix_snap_%d.bin", snap_count++);
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(tt, sizeof(*tt), 1, f);
            fclose(f);
            fprintf(stderr, "  [SNAP] wrote %s (%zu bytes)\n", path, sizeof(*tt));
        }
    }

    fprintf(stderr, "=== [TENSIX_STATE end] ===\n\n");
}
