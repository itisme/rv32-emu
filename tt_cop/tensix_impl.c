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

/* Default log level: only errors and warnings */
int tt_log_level = TT_LOG_WARN;

/* Initialize tensix coprocessor */
void tensix_init(tensix_t *tt,
                 uint8_t *l1_scratchpad_mem,
                 uint8_t *high_mem,
                 uint8_t *t0_ldm,
                 uint8_t *t1_ldm,
                 uint8_t *t2_ldm)
{
    if (!tt) {
        TT_ERR("Error: tensix_init called with NULL pointer\n");
        return;
    }

    /* Clear tensix state */
    memset(tt, 0, sizeof(tensix_t));

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

    tt->srca_dvalid = false;
    tt->srcb_dvalid = false;
    tt->dest_dvalid = false;
    tt->pack_l1_write_offset = 0;
    tt->pack_l1_dest_addr_raw = 0;

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
    tt->srca_dvalid = false;
    tt->srcb_dvalid = false;
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

        if (addr <= 0xFFE4FFFF) {
            /* INSTRN_BUF_BASE */
            if (core_id >= 0 && core_id <= 2) {
                /* Trisc0/1/2: push to own thread */
                while (!tensix_push(tt, data, core_id))
                    tensix_step(tt, core_id);
            } else if (core_id == -1) {
                /* Brisc: push to t0 */
                while (!tensix_push(tt, data, 0))
                    tensix_step(tt, 0);
            } else {
                /* NCrisc or invalid core_id: cannot push instructions */
                TT_WARN("[MMIO_WRITE_ERROR] core_id=%d attempted to write INSTRN_BUF_BASE, ignoring\n", core_id);
            }
        } else if (addr <= 0xFFE5FFFF) {
            /* INSTRN1_BUF_BASE */
            if (core_id == -1) {
                /* Brisc: push to t1 */
                while (!tensix_push(tt, data, 1))
                    tensix_step(tt, 1);
            } else if (core_id >= 0 && core_id <= 2) {
                /* Trisc0/1/2: will hang in real hardware */
                TT_WARN("[MMIO_WRITE_ERROR] core_id=%d (Trisc) attempted to write INSTRN1_BUF_BASE, would hang in real hardware\n", core_id);
            }
        } else {
            /* INSTRN2_BUF_BASE */
            if (core_id == -1) {
                /* Brisc: push to t2 */
                while (!tensix_push(tt, data, 2))
                    tensix_step(tt, 2);
            } else if (core_id >= 0 && core_id <= 2) {
                /* Trisc0/1/2: will hang in real hardware */
                TT_WARN("[MMIO_WRITE_ERROR] core_id=%d (Trisc) attempted to write INSTRN2_BUF_BASE, would hang in real hardware\n", core_id);
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
        if (reg_idx == 76 || reg_idx == 92 || reg_idx == 124 || reg_idx == 140 ||
            (reg_idx >= 64 && reg_idx <= 67) || (reg_idx >= 112 && reg_idx <= 115)) {
            TT_DBG("[CFG_MMIO] core=%d, cfg[%d] = 0x%x (addr=0x%x)\n",
                   core_id, reg_idx, data, addr);
        }
        return false; /* normal memory write to high_mem */
    }

    return false;
}
