/*
 * tensix_cop.c - Tensix Coprocessor implementation
 */

#include <string.h>
#include "tensix_cop.h"

/* Wait instruction opcodes (per ckernel_ops.h) */
#define OPCODE_STALLWAIT  0xA2  /* Line 881 in ckernel_ops.h */
#define OPCODE_SEMWAIT    0xA6  /* Line 474 in ckernel_ops.h */
#define OPCODE_STREAMWAIT 0xA7  /* Line 904 in ckernel_ops.h */

/* Other common opcodes (per ttinsn_code.txt / ckernel_ops.h) */
#define OPCODE_ATGETM     0xA0
#define OPCODE_ATRELM     0xA1
#define OPCODE_SEMINIT    0xA3
#define OPCODE_SEMPOST    0xA4
#define OPCODE_SEMGET     0xA5
#define OPCODE_NOP        0x02
#define OPCODE_PACR       0x41
#define OPCODE_UNPACR     0x42

/* Matrix Unit instructions require SrcA/SrcB dvalid */
#define OPCODE_ELWADD     0x28
#define OPCODE_ELWMUL     0x27
#define OPCODE_ELWSUB     0x30
#define OPCODE_DOTPV      0x29
#define OPCODE_MVMUL      0x26
#define OPCODE_CONV3S1    0x22
#define OPCODE_CONV3S2    0x23
#define OPCODE_MPOOL3S1   0x24
#define OPCODE_APOOL3S1   0x25
#define OPCODE_MPOOL3S2   0x31
#define OPCODE_APOOL3S2   0x32
#define OPCODE_GMPOOL     0x33
#define OPCODE_GAPOOL     0x34
#define OPCODE_MFCONV3S1  0x3a

/* Move instructions that use SrcA/SrcB/Dest as source */
#define OPCODE_MOVA2D     0x12
#define OPCODE_MOVB2D     0x13

/* Check if instruction requires SrcA dvalid */
static inline bool insn_needs_srca(uint32_t opcode)
{
    switch (opcode) {
    case OPCODE_ELWADD:
    case OPCODE_ELWMUL:
    case OPCODE_ELWSUB:
    case OPCODE_DOTPV:
    case OPCODE_MVMUL:
    case OPCODE_CONV3S1:
    case OPCODE_CONV3S2:
    case OPCODE_MPOOL3S1:
    case OPCODE_APOOL3S1:
    case OPCODE_MPOOL3S2:
    case OPCODE_APOOL3S2:
    case OPCODE_GMPOOL:
    case OPCODE_GAPOOL:
    case OPCODE_MFCONV3S1:
    case OPCODE_MOVA2D:
        return true;
    default:
        return false;
    }
}

/* Check if instruction requires SrcB dvalid */
static inline bool insn_needs_srcb(uint32_t opcode)
{
    switch (opcode) {
    case OPCODE_ELWADD:
    case OPCODE_ELWMUL:
    case OPCODE_ELWSUB:
    case OPCODE_DOTPV:
    case OPCODE_MVMUL:
    case OPCODE_CONV3S1:
    case OPCODE_CONV3S2:
        return true;
    default:
        return false;
    }
}

/* Check if instruction requires Dest dvalid (PACR needs Dest data ready) */
static inline bool insn_needs_dest(uint32_t opcode)
{
    switch (opcode) {
    case OPCODE_PACR:
        return true;
    default:
        return false;
    }
}

/* FIFO operations */
void tensix_fifo_init(tensix_fifo_t *fifo)
{
    memset(fifo, 0, sizeof(tensix_fifo_t));
    fifo->head = 0;
    fifo->tail = 0;
    fifo->count = 0;
}

bool tensix_fifo_is_empty(const tensix_fifo_t *fifo)
{
    return fifo->count == 0;
}

bool tensix_fifo_is_full(const tensix_fifo_t *fifo)
{
    return fifo->count >= TENSIX_FIFO_SIZE;
}

bool tensix_fifo_push(tensix_fifo_t *fifo, uint32_t insn)
{
    if (tensix_fifo_is_full(fifo))
        return false;

    fifo->buffer[fifo->tail] = insn;
    fifo->tail = (fifo->tail + 1) % TENSIX_FIFO_SIZE;
    fifo->count++;
    return true;
}

bool tensix_fifo_pop(tensix_fifo_t *fifo, uint32_t *insn)
{
    if (tensix_fifo_is_empty(fifo))
        return false;

    *insn = fifo->buffer[fifo->head];
    fifo->head = (fifo->head + 1) % TENSIX_FIFO_SIZE;
    fifo->count--;
    return true;
}

uint32_t tensix_fifo_count(const tensix_fifo_t *fifo)
{
    return fifo->count;
}

/* Initialize a thread */
static void tensix_cop_init_thread(tensix_thread_cop_t *thread)
{
    tensix_fifo_init(&thread->insn_fifo);
    thread->state = THREAD_STATE_IDLE;
    memset(&thread->wait_gate, 0, sizeof(thread->wait_gate));
    thread->current_insn = 0;
    thread->has_current_insn = false;
    thread->direct_insn = 0;
    thread->has_direct_insn = false;
    memset(&thread->mop_state, 0, sizeof(thread->mop_state));
    thread->insn_executed = 0;
    thread->cycles_idle = 0;
    thread->cycles_waiting = 0;
}

/* Coprocessor initialization */
void tensix_cop_init(tensix_cop_t *cop, tensix_t *core)
{
    memset(cop, 0, sizeof(tensix_cop_t));

    cop->core = core;
    cop->halted = false;
    cop->total_cycles = 0;

    /* Initialize all threads */
    for (int i = 0; i < 3; i++) {
        tensix_cop_init_thread(&cop->threads[i]);
    }

    /* Initialize memory callbacks to NULL */
    cop->mem_ctx = NULL;
    cop->mem_read_fn = NULL;
    cop->mem_write_fn = NULL;
}

/* Reset coprocessor */
void tensix_cop_reset(tensix_cop_t *cop)
{
    for (int i = 0; i < 3; i++) {
        tensix_cop_init_thread(&cop->threads[i]);
    }

    cop->halted = false;
    cop->total_cycles = 0;
}

/* Halt coprocessor */
void tensix_cop_halt(tensix_cop_t *cop)
{
    cop->halted = true;
}

/* Resume coprocessor */
void tensix_cop_resume(tensix_cop_t *cop)
{
    cop->halted = false;
}

/* Push instruction to thread FIFOs */
bool tensix_cop_push(tensix_cop_t *cop, int core_id, uint32_t insn)
{
    if (!cop || core_id < 0 || core_id >= TENSIX_NUM_CORES)
        return false;

    return tensix_fifo_push(&cop->threads[core_id].insn_fifo, insn);
}

bool tensix_cop_push_t0(tensix_cop_t *cop, uint32_t insn)
{
    return tensix_cop_push(cop, TENSIX_CORE_T0, insn);
}

bool tensix_cop_push_t1(tensix_cop_t *cop, uint32_t insn)
{
    return tensix_cop_push(cop, TENSIX_CORE_T1, insn);
}

bool tensix_cop_push_t2(tensix_cop_t *cop, uint32_t insn)
{
    return tensix_cop_push(cop, TENSIX_CORE_T2, insn);
}

/* Check FIFO status */
bool tensix_cop_t0_fifo_full(tensix_cop_t *cop)
{
    return tensix_fifo_is_full(&cop->threads[0].insn_fifo);
}

bool tensix_cop_t1_fifo_full(tensix_cop_t *cop)
{
    return tensix_fifo_is_full(&cop->threads[1].insn_fifo);
}

bool tensix_cop_t2_fifo_full(tensix_cop_t *cop)
{
    return tensix_fifo_is_full(&cop->threads[2].insn_fifo);
}

/* Set memory access callbacks */
void tensix_cop_set_memory(tensix_cop_t *cop, void *ctx,
                           uint32_t (*read_fn)(void *, uint32_t, uint32_t),
                           void (*write_fn)(void *, uint32_t, uint32_t, uint32_t))
{
    cop->mem_ctx = ctx;
    cop->mem_read_fn = read_fn;
    cop->mem_write_fn = write_fn;
}

/* Helper functions for instruction decode */
uint32_t tensix_insn_opcode(uint32_t insn)
{
    return (insn >> 24) & 0xFF;
}

uint32_t tensix_insn_param(uint32_t insn)
{
    return insn & 0x00FFFFFF;
}

/* Execute a Tensix instruction */
void tensix_cop_execute(tensix_cop_t *cop, int thread_id, uint32_t insn)
{
    tensix_t *core = cop->core;

    /* Use the jump table to execute the instruction */
    tensix_execute_insn(core, insn, thread_id);

    /* Instruction executed successfully */
    cop->threads[thread_id].insn_executed++;
}

/* ============================================================================
 * Wait Gate Operations
 * ============================================================================ */

/* Latch Wait instruction into Wait Gate */
/*
 * Return the BlockMask bits corresponding to an instruction's opcode.
 * Return value: bit0=B0, bit1=B1, ..., bit8=B8
 * Multiple bits can be set (e.g. some instructions are blocked by both B0 and B5)
 *
 * Reference: STALLWAIT.md Block mask table
 */
static uint32_t insn_get_block_bits(uint32_t opcode)
{
    /* Block mask bits (per STALLWAIT.md):
     * B0: Miscellaneous Unit, Mover, Scalar Unit, Packer, Unpacker
     * B1: Sync Unit
     * B2: Packer
     * B3: Unpacker
     * B4: Mover (XMOV)
     * B5: Scalar Unit (ThCon)
     * B6: Matrix Unit (FPU)
     * B7: Configuration Unit
     * B8: Vector Unit (SFPU)
     *
     * Opcodes per ttinsn_code.txt (Blackhole definitions)
     */

    switch (opcode) {
    /* ---- B0 only: Miscellaneous ---- */
    case 0x53:  /* ADDRCRXY */
    case 0x56:  /* ADDRCRZW */
    case 0x52:  /* INCADCXY */
    case 0x55:  /* INCADCZW */
    case 0x44:  /* RSTDMA */
    case 0x50:  /* SETADC */
    case 0x5e:  /* SETADCXX */
    case 0x51:  /* SETADCXY */
    case 0x54:  /* SETADCZW */
    case 0x57:  /* SETDVALID */
        return (1 << 0);  /* B0 */

    /* ---- B1 only: Sync Unit ---- */
    case 0xa0:  /* ATGETM */
    case 0xa1:  /* ATRELM */
    case 0xa5:  /* SEMGET */
    case 0xa3:  /* SEMINIT */
    case 0xa4:  /* SEMPOST */
        return (1 << 1);  /* B1 */

    /* ---- B0 + B2: Packer ---- */
    case 0x41:  /* PACR */
    case 0x4a:  /* PACR_SETREG */
        return (1 << 0) | (1 << 2);  /* B0 + B2 */

    /* ---- B0 + B3: Unpacker ---- */
    case 0x42:  /* UNPACR */
    case 0x43:  /* UNPACR_NOP */
        return (1 << 0) | (1 << 3);  /* B0 + B3 */

    /* ---- B0 + B4: Mover ---- */
    case 0x40:  /* XMOV */
        return (1 << 0) | (1 << 4);  /* B0 + B4 */

    /* ---- B0 + B5: Scalar Unit (ThCon) ---- */
    case 0x58:  /* ADDDMAREG */
    case 0x64:  /* ATCAS */
    case 0x61:  /* ATINCGET */
    case 0x62:  /* ATINCGETPTR */
    case 0x63:  /* ATSWAP */
    case 0x5b:  /* BITWOPDMAREG */
    case 0x5d:  /* CMPDMAREG */
    case 0x60:  /* DMANOP */
    case 0x46:  /* FLUSHDMA */
    case 0x49:  /* LOADIND */
    case 0x68:  /* LOADREG */
    case 0x5a:  /* MULDMAREG */
    case 0x48:  /* REG2FLOP */
    case 0x45:  /* SETDMAREG */
    case 0x5c:  /* SHIFTDMAREG */
    case 0x66:  /* STOREIND */
    case 0x67:  /* STOREREG */
    case 0x59:  /* SUBDMAREG */
        return (1 << 0) | (1 << 5);  /* B0 + B5 */

    /* ---- B6: Matrix Unit (FPU) ---- */
    case 0x25:  /* APOOL3S1 */
    case 0x32:  /* APOOL3S2 */
    case 0x36:  /* CLEARDVALID */
    case 0x21:  /* CLREXPHIST */
    case 0x22:  /* CONV3S1 */
    case 0x23:  /* CONV3S2 */
    case 0x29:  /* DOTPV */
    case 0x28:  /* ELWADD */
    case 0x27:  /* ELWMUL */
    case 0x30:  /* ELWSUB */
    case 0x34:  /* GAPOOL */
    case 0x35:  /* GATESRCRST */
    case 0x33:  /* GMPOOL */
    case 0x38:  /* INCRWC */
    case 0x3a:  /* MFCONV3S1 */
    case 0x12:  /* MOVA2D */
    case 0x0b:  /* MOVB2A */
    case 0x13:  /* MOVB2D */
    case 0x08:  /* MOVD2A */
    case 0x0a:  /* MOVD2B */
    case 0x09:  /* MOVDBGA2D */
    case 0x24:  /* MPOOL3S1 */
    case 0x31:  /* MPOOL3S2 */
    case 0x26:  /* MVMUL */
    case 0x37:  /* SETRWC */
    case 0x17:  /* SHIFTXA */
    case 0x18:  /* SHIFTXB */
    case 0x16:  /* TRNSPSRCB */
    case 0x10:  /* ZEROACC */
    case 0x11:  /* ZEROSRC */
        return (1 << 6);  /* B6 */

    /* ---- B7: Configuration Unit ---- */
    case 0xb8:  /* CFGSHIFTMASK */
    case 0xb1:  /* RDCFG */
    case 0xb3:  /* RMWCIB0 */
    case 0xb4:  /* RMWCIB1 */
    case 0xb5:  /* RMWCIB2 */
    case 0xb6:  /* RMWCIB3 */
    case 0xb2:  /* SETC16 */
    case 0xb7:  /* STREAMWRCFG */
    case 0xb0:  /* WRCFG */
        return (1 << 7);  /* B7 */

    /* ---- B8: Vector Unit (SFPU) ---- */
    case 0x7d:  /* SFPABS */
    case 0x85:  /* SFPADD */
    case 0x75:  /* SFPADDI */
    case 0x7e:  /* SFPAND */
    case 0x99:  /* SFPARECIP */
    case 0x90:  /* SFPCAST */
    case 0x8b:  /* SFPCOMPC */
    case 0x91:  /* SFPCONFIG */
    case 0x76:  /* SFPDIVP2 */
    case 0x8a:  /* SFPENCC */
    case 0x77:  /* SFPEXEXP */
    case 0x78:  /* SFPEXMAN */
    case 0x97:  /* SFPGT */
    case 0x79:  /* SFPIADD */
    case 0x96:  /* SFPLE */
    case 0x70:  /* SFPLOAD */
    case 0x71:  /* SFPLOADI */
    case 0x93:  /* SFPLOADMACRO */
    case 0x73:  /* SFPLUT */
    case 0x95:  /* SFPLUTFP32 */
    case 0x81:  /* SFPLZ */
    case 0x84:  /* SFPMAD */
    case 0x7c:  /* SFPMOV */
    case 0x86:  /* SFPMUL */
    case 0x98:  /* SFPMUL24 */
    case 0x74:  /* SFPMULI */
    case 0x8f:  /* SFPNOP */
    case 0x80:  /* SFPNOT */
    case 0x7f:  /* SFPOR */
    case 0x88:  /* SFPPOPC */
    case 0x87:  /* SFPPUSHC */
    case 0x7b:  /* SFPSETCC */
    case 0x82:  /* SFPSETEXP */
    case 0x83:  /* SFPSETMAN */
    case 0x89:  /* SFPSETSGN */
    case 0x7a:  /* SFPSHFT */
    case 0x94:  /* SFPSHFT2 */
    case 0x8e:  /* SFP_STOCH_RND */
    case 0x72:  /* SFPSTORE */
    case 0x92:  /* SFPSWAP */
    case 0x8c:  /* SFPTRANSP */
    case 0x8d:  /* SFPXOR */
        return (1 << 8);  /* B8 */

    /* ---- All bits: Wait instructions ---- */
    case 0xa2:  /* STALLWAIT */
    case 0xa6:  /* SEMWAIT */
    case 0xa7:  /* STREAMWAIT */
        return 0x1FF;  /* All 9 bits */

    /* NOP special handling: only blocked when all block bits are set */
    case 0x02:  /* NOP */
        return 0;  /* NOP is not blocked by default, unless block_mask == 0x1FF */

    /* MOP(0x01), MOP_CFG(0x03), REPLAY(0x04), RESOURCEDECL(0x05)
     * These instructions never reach the Wait Gate (handled by MOP/Replay Expander)
     * Other unlisted instructions are also not blocked
     */
    default:
        return 0;
    }
}

static void wait_gate_latch(tensix_cop_t *cop, int core_id, uint32_t insn)
{
    tensix_wait_gate_t *gate = &cop->threads[core_id].wait_gate;
    uint32_t opcode = tensix_insn_opcode(insn);
    uint32_t param = tensix_insn_param(insn);

    gate->active = true;
    gate->opcode = opcode;

    if (opcode == OPCODE_STALLWAIT) {
        /* STALLWAIT (0xA2):
         * TT_OP_STALLWAIT(stall_res, wait_res)
         * Bits [23:15] = stall_res (BlockMask, 9 bits)
         * Bits [14:0]  = wait_res (ConditionMask, 15 bits)
         */
        gate->block_mask = (param >> 15) & 0x1FF;      // 9 bits
        gate->condition_mask = param & 0x7FFF;         // 15 bits
        gate->semaphore_mask = 0;

        /* Default value handling (per ISA documentation) */
        if (gate->condition_mask == 0)
            gate->condition_mask = 0x0F;
        if (gate->block_mask == 0)
            gate->block_mask = (1 << 6);

    } else if (opcode == OPCODE_SEMWAIT) {
        /* SEMWAIT (0xA6):
         * TT_OP_SEMWAIT(stall_res, sem_sel, wait_sem_cond)
         * Bits [23:15] = stall_res (BlockMask, 9 bits)
         * Bits [14:2]  = sem_sel (SemaphoreMask, 13 bits, only low 8 bits used)
         * Bits [1:0]   = wait_sem_cond (ConditionMask, 2 bits)
         */
        gate->block_mask = (param >> 15) & 0x1FF;      // 9 bits
        gate->semaphore_mask = (param >> 2) & 0x1FFF;  // 13 bits (only low 8 bits used)
        gate->condition_mask = param & 0x03;           // 2 bits

        /* Default value handling */
        if (gate->block_mask == 0)
            gate->block_mask = (1 << 6);

        /* If ConditionMask == 0, degenerate to STALLWAIT */
        if (gate->condition_mask == 0) {
            gate->opcode = OPCODE_STALLWAIT;
            gate->condition_mask = 0x0F;
            gate->semaphore_mask = 0;
        }
        TT_DBG("[SEMWAIT_LATCH] core=%d block=0x%x sem_mask=0x%x cond=0x%x insn=0x%08x\n",
               core_id, gate->block_mask, gate->semaphore_mask, gate->condition_mask, insn);

    } else if (opcode == OPCODE_STREAMWAIT) {
        /* STREAMWAIT (0xA7):
         * TT_OP_STREAMWAIT(stall_res, target_value, target_sel, wait_stream_sel)
         * Bits [23:15] = stall_res (BlockMask, 9 bits)
         * Bits [14:4]  = target_value (TargetValueLo, 11 bits)
         * Bits [3]     = target_sel (ConditionIndex, 1 bit)
         * Bits [2:0]   = wait_stream_sel (StreamSelect, 3 bits)
         */
        gate->block_mask = (param >> 15) & 0x1FF;      // 9 bits
        uint32_t target_lo = (param >> 4) & 0x7FF;     // 11 bits
        uint32_t cond_idx = (param >> 3) & 0x1;        // 1 bit
        gate->stream_select = param & 0x7;             // 3 bits

        /* Default value handling */
        if (gate->block_mask == 0)
            gate->block_mask = (1 << 6);

        /* Convert ConditionIndex to ConditionMask */
        gate->condition_mask = 1 << cond_idx;

        /* TargetValue high bits need to be read from ThreadConfig */
        gate->target_value = target_lo;
    }
}

/* Check if Wait Gate conditions are satisfied */
static bool wait_gate_check(tensix_cop_t *cop, int core_id)
{
    tensix_wait_gate_t *gate = &cop->threads[core_id].wait_gate;
    tensix_t *core = cop->core;

    if (!gate->active)
        return true;

    if (gate->opcode == OPCODE_STALLWAIT) {
        /* Check STALLWAIT conditions (13-bit ConditionMask, C0-C12)
         *
         * Semantics: if any selected condition is true ("keep waiting"), keep waiting.
         * Only when all selected conditions are false does the gate open.
         *
         * Reference: tt-isa-documentation/BlackholeA0/TensixTile/TensixCoprocessor/STALLWAIT.md
         */
        uint32_t cond = gate->condition_mask;

        /* C0: Scalar Unit (ThCon) has outstanding memory requests.
         * In synchronous simulator, all memory requests complete instantly (not waiting). */

        /* C1: Unpacker 0 pipeline busy for current thread.
         * C2: Unpacker 1 pipeline busy for current thread.
         * C3: Packer pipeline busy for current thread.
         * C4: Matrix Unit (FPU) pipeline busy for current thread.
         * In synchronous simulator, instructions complete in the same step, pipeline always empty. */

        /* C5: SrcA AllowedClient != Unpackers
         * i.e. SrcA is currently owned by MatrixUnit (srca_dvalid=true), Unpacker cannot write */
        if ((cond & (1 << 5)) && core->srca_dvalid)
            return false;

        /* C6: SrcB AllowedClient != Unpackers */
        if ((cond & (1 << 6)) && core->srcb_dvalid)
            return false;

        /* C7: SrcA AllowedClient != MatrixUnit
         * i.e. SrcA is currently owned by Unpackers (srca_dvalid=false), MatrixUnit cannot read */
        if ((cond & (1 << 7)) && !core->srca_dvalid)
            return false;

        /* C8: SrcB AllowedClient != MatrixUnit */
        if ((cond & (1 << 8)) && !core->srcb_dvalid)
            return false;

        /* C9: Mover has outstanding memory requests.
         * C10: SrcA AllowedClient != MatrixUnit
         * C11: SrcB AllowedClient != MatrixUnit  
         * C12: Configuration Unit pipeline busy (any thread).
         *
         * In synchronous simulator these conditions are always met (pipeline/requests completed).
         * TODO: Properly implement C10/C11 by tracking AllowedClient state.
         * For now, C9-C12 always return true (conditions met).
         * Note: C10/C11 would require SETDVALID to set srca_dvalid/srcb_dvalid,
         * but this needs proper integration with Unpacker flow.
         */
        (void)cond;  /* Suppress unused variable warning */
        (void)core;  /* Suppress unused variable warning */

        return true;

    } else if (gate->opcode == OPCODE_SEMWAIT) {
        /* Check SEMWAIT conditions */
        uint32_t sem_mask = gate->semaphore_mask;
        uint32_t cond_mask = gate->condition_mask;
        TT_DBG("[SEMWAIT_CHECK] core=%d sem_mask=0x%x cond=0x%x sem0=%d sem1=%d\n",
               core_id, sem_mask, cond_mask, core->sem[0], core->sem[1]);

        /* C0: Any selected semaphore Value == 0 */
        if (cond_mask & 0x1) {
            for (int i = 0; i < 8; i++) {
                if (sem_mask & (1 << i)) {
                    if (core->sem[i] == 0)
                        return false;  /* Condition not met */
                }
            }
        }

        /* C1: Any selected semaphore Value >= Max */
        if (cond_mask & 0x2) {
            for (int i = 0; i < 8; i++) {
                if (sem_mask & (1 << i)) {
                    if (core->sem[i] >= core->sem_max[i])
                        return false;  /* Condition not met */
                }
            }
        }

        return true;  /* All conditions met */

    } else if (gate->opcode == OPCODE_STREAMWAIT) {
        /* Check STREAMWAIT conditions */
        /* TODO: Implement NoC Overlay stream condition check */
        return true;
    }

    return true;
}

/* Clear Wait Gate */
static void wait_gate_clear(tensix_cop_t *cop, int core_id)
{
    tensix_wait_gate_t *gate = &cop->threads[core_id].wait_gate;
    TT_DBG("[WAITGATE_CLEAR] core=%d opcode=0x%02x\n", core_id, gate->opcode);
    memset(gate, 0, sizeof(*gate));
}

/* Old switch-based implementation kept for reference but not used */
#if 0
void tensix_cop_execute_old(tensix_cop_t *cop, int thread_id, uint32_t insn)
{
    uint32_t opcode = tensix_insn_opcode(insn);
    uint32_t param = tensix_insn_param(insn);

    tensix_t *core = cop->core;

    /* Decode and execute instruction based on opcode */
    switch (opcode) {
    /* T0 Thread Instructions */
    case 0x44: /* TTZEROSRC - Zero source registers */
    {
        uint32_t dest_reg = param & 0xFF;
        if (dest_reg < 64) {
            core->sreg_a[dest_reg] = 0;
        }
        break;
    }

    case 0x45: /* TTSETADCXY - Set address configuration XY */
    {
        uint32_t x_coord = (param >> 0) & 0xFF;
        uint32_t y_coord = (param >> 8) & 0xFF;
        core->adc_cfg.x = x_coord;
        core->adc_cfg.y = y_coord;
        break;
    }

    case 0x51: /* TTSETADCZW - Set address configuration ZW */
    {
        uint32_t z_coord = (param >> 0) & 0xFF;
        uint32_t w_coord = (param >> 8) & 0xFF;
        core->adc_cfg.z = z_coord;
        core->adc_cfg.w = w_coord;
        break;
    }

    case 0xA0: /* TTATGETM - Mutex Get */
    {
        uint32_t mutex_id = param & 0xFF;
        if (mutex_id < 8) {
            if (!core->mutex[mutex_id]) {
                core->mutex[mutex_id] = true;
            } else {
                /* Mutex taken - stall thread */
                cop->threads[thread_id].state = THREAD_STATE_WAITING;
                return;
            }
        }
        break;
    }

    case 0xA1: /* TTATRELM - Mutex Release */
    {
        uint32_t mutex_id = param & 0xFF;
        if (mutex_id < 8) {
            core->mutex[mutex_id] = false;
        }
        break;
    }

    case 0x78: /* TTSETADCXX - Set address configuration extended */
    {
        uint32_t cfg_val = param;
        core->adc_cfg.extended = cfg_val;
        break;
    }

    case 0xB5: /* TTSETC16 - Set 16-bit config */
    {
        uint32_t cfg_idx = (param >> 16) & 0xFF;
        uint32_t cfg_val = param & 0xFFFF;
        if (cfg_idx < 16) {
            core->cfg16[cfg_idx] = cfg_val;
        }
        break;
    }

    case 0xAF: /* TTSTALLWAIT - Stall and wait */
    {
        uint32_t cycles = param & 0xFFFF;
        if (cycles > 0) {
            cop->threads[thread_id].state = THREAD_STATE_STALLED;
            cop->threads[thread_id].stall_cycles = cycles;
        }
        break;
    }

    case 0x01: /* TTMOP - Macro Operation */
    {
        uint32_t mop_id = param & 0xFF;
        /* Expand MOP into actual instructions and execute them */
        TT_DBG("[MOP] Expanding MOP, param=0x%06x, mop_id=%d, core=%d\n", param, mop_id, thread_id);
        tensix_cop_mop_expand(cop, thread_id, param);
        if (mop_id < 16) {
            core->mop_state[mop_id] = 1;
        }
        break;
    }

    case 0x94: /* TTSEMGET - Semaphore Get (nonblocking check) */
    {
        uint32_t sem_id = param & 0xFF;
        if (sem_id < 8) {
            /* Set result register based on semaphore availability */
            core->sem_result = (core->sem[sem_id] > 0) ? 1 : 0;
        }
        break;
    }

    /* T1 Thread Instructions */
    case 0xB0: /* TTSEMINIT - Semaphore Initialize */
    {
        uint32_t sem_id = param & 0xFF;
        uint32_t max_val = (param >> 8) & 0xFFFF;
        if (sem_id < 8) {
            core->sem[sem_id] = 0;
            core->sem_max[sem_id] = max_val;
        }
        break;
    }

    case 0x37: /* TTSETRWC - Set read/write configuration */
    {
        uint32_t rw_mode = (param >> 0) & 0xFF;
        uint32_t rw_addr = (param >> 8) & 0xFFFF;
        core->rw_cfg.mode = rw_mode;
        core->rw_cfg.addr = rw_addr;
        break;
    }

    case 0xB1: /* TTSEMWAIT - Semaphore Wait */
    {
        uint32_t sem_id = param & 0xFF;
        if (sem_id < 8) {
            if (core->sem[sem_id] > 0) {
                core->sem[sem_id]--;
            } else {
                /* Stall this thread */
                cop->threads[thread_id].state = THREAD_STATE_WAITING;
                return; /* Don't increment insn counter */
            }
        }
        break;
    }

    case 0xB2: /* TTSEMPOST - Semaphore Post */
    {
        uint32_t sem_id = param & 0xFF;
        if (sem_id < 8) {
            if (core->sem[sem_id] < core->sem_max[sem_id]) {
                core->sem[sem_id]++;
            }
        }
        break;
    }

    /* T2 Thread Instructions */
    case 0xB6: /* TTWRCFG - Write Configuration */
    {
        uint32_t cfg_addr = (param >> 0) & 0xFFFF;
        uint32_t cfg_val = (param >> 16) & 0xFF;
        /* Write to configuration space */
        if (cfg_addr < 1024) {
            core->cfg_regs[cfg_addr] = cfg_val;
        }
        break;
    }

    case 0x02: /* TTNOP - No Operation */
        /* Do nothing */
        break;

    case 0x60: /* TTDMANOP - DMA No Operation */
        /* DMA pipeline bubble */
        break;

    case 0x42: /* TTUNPACR - Unpack operation */
    {
        uint32_t src_reg = (param >> 0) & 0xFF;
        uint32_t dst_reg = (param >> 8) & 0xFF;
        uint32_t format = (param >> 16) & 0xFF;

        /* Functional simulation: just copy data with format marker */
        if (src_reg < 64 && dst_reg < 64) {
            core->sreg_b[dst_reg] = core->sreg_a[src_reg];
            core->unpack_format = format;
        }
        break;
    }

    case 0x28: /* TTELWADD - Element-wise Add */
    {
        uint32_t src1 = (param >> 0) & 0xFF;
        uint32_t src2 = (param >> 8) & 0xFF;
        uint32_t dst = (param >> 16) & 0xFF;

        /* Functional simulation: mark operation as done */
        if (src1 < 64 && src2 < 64 && dst < 64) {
            core->math_op_pending = false;
            core->last_math_op = 0x28;
        }
        break;
    }

    case 0x41: /* TTPACR - Pack operation */
    {
        uint32_t src_reg = (param >> 0) & 0xFF;
        uint32_t dst_reg = (param >> 8) & 0xFF;
        uint32_t format = (param >> 16) & 0xFF;

        /* Functional simulation: just copy data with format marker */
        if (src_reg < 64 && dst_reg < 64) {
            core->sreg_a[dst_reg] = core->sreg_b[src_reg];
            core->pack_format = format;
        }
        break;
    }

    /* Additional instructions kept for compatibility */
    case 0x03: /* MOP_CFG */
        /* Configure MOP */
        break;

    case 0xB8: /* CFGSHIFTMASK */
        /* Configure shift/mask */
        break;

    /* SFPU instructions */
    case 0xC0: /* SFPADD */
    case 0xC1: /* SFPMUL */
    case 0xC2: /* SFPLOAD */
    case 0xC3: /* SFPSTORE */
    case 0xC4: /* SFPMOV */
    case 0xC5: /* SFPNOP */
        /* SFPU operations - functional only */
        break;

    /* Pack/Unpack instructions */
    case 0x12: /* MOVA2D */
    case 0x0B: /* MOVB2A */
        /* Move operations */
        break;

    /* Math operations */
    case 0x22: /* CONV3S1 */
    case 0x23: /* CONV3S2 */
    case 0x24: /* MATMUL */
    case 0x25: /* APOOL3S1 */
    case 0x27: /* ELWMUL */
        /* Math operations - functional only */
        break;

    default:
        /* Unknown instruction - just ignore for now */
        TT_WARN("Tensix thread %d: unknown opcode 0x%02X\n", thread_id, opcode);
        break;
    }

    /* Instruction executed successfully */
    cop->threads[thread_id].insn_executed++;
}
#endif

/* ============================================================================
 * Step Interface (supports core_id and Wait Gate)
 * ============================================================================ */

/* Execute one instruction for a given core's thread
 *
 * Correct STALLWAIT behavior:
 * 1. STALLWAIT does not block the thread, only sets Wait Gate state
 * 2. Subsequent instructions popped from FIFO are checked against BlockMask
 * 3. Only instructions matching BlockMask need ConditionMask check
 * 4. Instructions not matching BlockMask pass through directly
 */
bool tensix_cop_step(tensix_cop_t *cop, int core_id)
{
    if (!cop || core_id < 0 || core_id >= TENSIX_NUM_CORES)
        return false;

    if (cop->halted)
        return false;

    tensix_thread_cop_t *thread = &cop->threads[core_id];
    uint32_t insn;

    /* Step 1: Fetch instruction (prefer pending instruction, otherwise pop from FIFO) */
    if (thread->has_current_insn) {
        /* Has pending instruction (blocked from last cycle) */
        insn = thread->current_insn;
        thread->has_current_insn = false;
    } else {
        /* Pop new instruction from FIFO */
        if (!tensix_fifo_pop(&thread->insn_fifo, &insn)) {
            /* FIFO empty */
            thread->state = THREAD_STATE_IDLE;
            thread->cycles_idle++;
            return false;
        }
    }

    uint32_t opcode = tensix_insn_opcode(insn);

    /* Step 2: Check if instruction is blocked by Wait Gate
     *
     * Per ISA docs (STALLWAIT.md block-mask table), STALLWAIT/SEMWAIT/STREAMWAIT
     * themselves are also blocked by all B0-B8. From the documentation:
     *   "once the first blocked instruction reaches the Wait Gate, no instructions
     *    of any kind can pass through the Wait Gate until the STALLWAIT's conditions
     *    are met."
     *
     * Therefore wait gate check must happen before wait instruction latch, otherwise
     * a new wait instruction would incorrectly overwrite the old unsatisfied wait gate.
     *
     * MOP_CFG does not go through Wait Gate (marked N/A in docs), so handle it first.
     */

    /* MOP_CFG bypasses Wait Gate (handled by MOP Expander) */
    if (opcode == 0x03) {  /* MOP_CFG */
        thread->zmask_hi16 = insn & 0x00FFFFFF;
        thread->state = THREAD_STATE_RUNNING;
        thread->insn_executed++;
        return true;  /* MOP_CFG executed */
    }

    /* Check if Wait Gate blocks the current instruction */
    if (thread->wait_gate.active) {
        /* Get the BlockMask bits for this instruction */
        uint32_t insn_block_bits = insn_get_block_bits(opcode);

        /* NOP special handling: only blocked when all block bits are set */
        if (opcode == OPCODE_NOP && thread->wait_gate.block_mask == 0x1FF) {
            insn_block_bits = 0x1FF;
        }

        /* Check if instruction matches BlockMask */
        if (insn_block_bits & thread->wait_gate.block_mask) {
            /* Instruction matches BlockMask, need to check conditions */
            if (!wait_gate_check(cop, core_id)) {
                /* Conditions not met, instruction is blocked */
                thread->current_insn = insn;
                thread->has_current_insn = true;
                thread->state = THREAD_STATE_WAITING;
                thread->cycles_waiting++;
                return false;  /* Instruction blocked, not executed */
            }

            /* Conditions met, clear Wait Gate and continue execution */
            wait_gate_clear(cop, core_id);
        }
        /* Instruction does not match BlockMask, pass through (skip condition check) */
    }

    /* Step 3: If this is a Wait instruction, latch it into Wait Gate */
    if (opcode == OPCODE_STALLWAIT || opcode == OPCODE_SEMWAIT ||
        opcode == OPCODE_STREAMWAIT) {
        wait_gate_latch(cop, core_id, insn);
        thread->state = THREAD_STATE_RUNNING;
        thread->insn_executed++;
        return true;  /* Wait instruction executed, thread continues */
    }

    /* Step 4: dvalid check (hardware auto-wait mechanism)
     *
     * Per ISA docs, Matrix Unit (FPU) instructions (ELWADD etc.) auto-wait
     * for SrcA/SrcB data valid (AllowedClient == MatrixUnit) before executing.
     * Packer instructions (PACR) auto-wait for Dest data valid.
     * UNPACR auto-waits for target bank to be owned by Unpackers (dvalid == false).
     * This is the key synchronization mechanism for T0(unpacr)<->T1(math)->T2(pacr) pipeline.
     */
    if (insn_needs_srca(opcode) && !cop->core->srca_dvalid) {
        thread->current_insn = insn;
        thread->has_current_insn = true;
        thread->state = THREAD_STATE_WAITING;
        return false;
    }
    if (insn_needs_srcb(opcode) && !cop->core->srcb_dvalid) {
        thread->current_insn = insn;
        thread->has_current_insn = true;
        thread->state = THREAD_STATE_WAITING;
        return false;
    }
    if (insn_needs_dest(opcode) && !cop->core->dest_dvalid) {
        thread->current_insn = insn;
        thread->has_current_insn = true;
        thread->state = THREAD_STATE_WAITING;
        return false;
    }

    /* UNPACR reverse dvalid: wait for target bank to be owned by Unpackers.
     * In hardware, UNPACR auto-waits when AllowedClient != Unpackers.
     * which_unp: 0=SrcA, 1=SrcB (insn bit 23)
     */
    if (opcode == OPCODE_UNPACR) {
        uint32_t which_unp = (insn >> 23) & 0x1;
        if (which_unp == 0 && cop->core->srca_dvalid) {
            thread->current_insn = insn;
            thread->has_current_insn = true;
            thread->state = THREAD_STATE_WAITING;
            return false;
        }
        if (which_unp == 1 && cop->core->srcb_dvalid) {
            thread->current_insn = insn;
            thread->has_current_insn = true;
            thread->state = THREAD_STATE_WAITING;
            return false;
        }
    }

    /* Step 5: Execute instruction */
    if (!tensix_cop_execute_insn(cop, core_id, insn)) {
        /* Instruction not completed (e.g. MOP expansion in progress), save state and return false */
        thread->current_insn = insn;
        thread->has_current_insn = true;
        thread->state = THREAD_STATE_WAITING;
        thread->cycles_waiting++;
        return false;
    }
    thread->state = THREAD_STATE_RUNNING;
    thread->insn_executed++;
    return true;
}

/* Execute one instruction for all cores' threads */
int tensix_cop_step_all(tensix_cop_t *cop)
{
    if (!cop)
        return 0;

    int count = 0;

    if (tensix_cop_step(cop, TENSIX_CORE_T0))
        count++;
    if (tensix_cop_step(cop, TENSIX_CORE_T1))
        count++;
    if (tensix_cop_step(cop, TENSIX_CORE_T2))
        count++;

    cop->total_cycles++;
    return count;
}

/* Execute a single core until FIFO empty or Wait encountered */
int tensix_cop_run_until_wait(tensix_cop_t *cop, int core_id, int max_insns)
{
    int executed = 0;

    for (int i = 0; i < max_insns; i++) {
        if (!tensix_cop_step(cop, core_id)) {
            /* Hit wait or FIFO empty */
            break;
        }
        executed++;
    }

    return executed;
}

/* Execute all cores until all FIFOs empty or Wait encountered */
int tensix_cop_run_until_wait_all(tensix_cop_t *cop, int max_insns)
{
    int total = 0;
    total += tensix_cop_run_until_wait(cop, TENSIX_CORE_T0, max_insns);
    total += tensix_cop_run_until_wait(cop, TENSIX_CORE_T1, max_insns);
    total += tensix_cop_run_until_wait(cop, TENSIX_CORE_T2, max_insns);
    return total;
}

/* Set direct instruction (for do_ttinsn, bypasses FIFO) */
void tensix_cop_set_direct(tensix_cop_t *cop, int core_id, uint32_t insn)
{
    if (!cop || core_id < 0 || core_id >= TENSIX_NUM_CORES)
        return;

    tensix_thread_cop_t *thread = &cop->threads[core_id];
    thread->direct_insn = insn;
    thread->has_direct_insn = true;
}

/* Execute direct instruction (for do_ttinsn, with blocking support)
 * Returns: true = instruction completed, PC should advance
 *          false = blocked by Wait Gate, PC should not advance
 */
/* Forward declaration: MOP continuation is defined after the MOP expander */
static bool mop_continue_execution(tensix_cop_t *cop, int core_id);

bool tensix_cop_step_direct(tensix_cop_t *cop, int core_id)
{
    if (!cop || core_id < 0 || core_id >= TENSIX_NUM_CORES)
        return false;

    if (cop->halted)
        return false;

    tensix_thread_cop_t *thread = &cop->threads[core_id];

    /* Check if there is a direct instruction pending */
    if (!thread->has_direct_insn)
        return true;  /* No instruction, treat as completed */

    /* --- MOP continuation: generate and execute expanded instructions one by one --- */
    if (thread->mop_state.active) {
        return mop_continue_execution(cop, core_id);
    }

    /* Drain pending FIFO instructions before executing the direct instruction.
     * Memory-mapped COP writes (e.g., SETDMAREG via 'sw' to INSTRN_BUF) are
     * pushed to the FIFO, while embedded COP instructions (ttwrcfg, etc.) go
     * through this direct path.  In real hardware they share a single pipeline
     * and STALLWAIT enforces ordering.  We must drain the FIFO here so that
     * e.g. SETDMAREG updates dma_reg[] before a subsequent WRCFG reads it.
     */
    while (tensix_cop_step(cop, core_id))
        ;

    uint32_t insn = thread->direct_insn;
    uint32_t opcode = tensix_insn_opcode(insn);

    /* MOP_CFG bypasses Wait Gate (handled by MOP Expander) */
    if (opcode == 0x03) {  /* MOP_CFG */
        thread->zmask_hi16 = insn & 0x00FFFFFF;
        thread->has_direct_insn = false;
        thread->state = THREAD_STATE_RUNNING;
        thread->insn_executed++;
        return true;
    }

    /* Check if Wait Gate blocks this instruction (including STALLWAIT/SEMWAIT/STREAMWAIT themselves) */
    if (thread->wait_gate.active) {
        uint32_t insn_block_bits = insn_get_block_bits(opcode);

        /* NOP special handling */
        if (opcode == OPCODE_NOP && thread->wait_gate.block_mask == 0x1FF) {
            insn_block_bits = 0x1FF;
        }

        /* Check if instruction matches BlockMask */
        if (insn_block_bits & thread->wait_gate.block_mask) {
            if (!wait_gate_check(cop, core_id)) {
                /* Conditions not met, instruction is blocked */
                thread->state = THREAD_STATE_WAITING;
                thread->cycles_waiting++;
                return false;  /* Keep has_direct_insn = true */
            }
            /* Conditions met, clear Wait Gate */
            wait_gate_clear(cop, core_id);
        }
    }

    /* If this is a Wait instruction, after passing Wait Gate, latch it into Wait Gate */
    if (opcode == OPCODE_STALLWAIT || opcode == OPCODE_SEMWAIT ||
        opcode == OPCODE_STREAMWAIT) {
        wait_gate_latch(cop, core_id, insn);
        thread->has_direct_insn = false;
        thread->state = THREAD_STATE_RUNNING;
        thread->insn_executed++;
        return true;
    }

    /* dvalid check (hardware auto-wait mechanism) */
    if (insn_needs_srca(opcode) && !cop->core->srca_dvalid) {
        thread->state = THREAD_STATE_WAITING;
        return false;  /* Keep has_direct_insn = true */
    }
    if (insn_needs_srcb(opcode) && !cop->core->srcb_dvalid) {
        thread->state = THREAD_STATE_WAITING;
        return false;
    }
    if (insn_needs_dest(opcode) && !cop->core->dest_dvalid) {
        thread->state = THREAD_STATE_WAITING;
        return false;
    }

    /* UNPACR reverse dvalid: wait for target bank to be owned by Unpackers */
    if (opcode == OPCODE_UNPACR) {
        uint32_t which_unp = (insn >> 23) & 0x1;
        if (which_unp == 0 && cop->core->srca_dvalid) {
            thread->state = THREAD_STATE_WAITING;
            return false;
        }
        if (which_unp == 1 && cop->core->srcb_dvalid) {
            thread->state = THREAD_STATE_WAITING;
            return false;
        }
    }

    /* Execute instruction */
    if (!tensix_cop_execute_insn(cop, core_id, insn)) {
        /* Instruction not completed, save state and return false */
        thread->state = THREAD_STATE_WAITING;
        thread->cycles_waiting++;
        return false;  /* Keep has_direct_insn = true */
    }

    /* --- MOP special handling: ttmop initialized expansion state, start executing one by one --- */
    if (opcode == 0x01) {
        /* mop_state was initialized by ttmop -> tensix_cop_mop_expand */
        return mop_continue_execution(cop, core_id);
    }

    thread->has_direct_insn = false;
    thread->state = THREAD_STATE_RUNNING;
    thread->insn_executed++;
    return true;
}

/* Check if a core is blocked by Wait Gate */
bool tensix_cop_is_waiting(tensix_cop_t *cop, int core_id)
{
    if (!cop || core_id < 0 || core_id >= TENSIX_NUM_CORES)
        return false;

    return cop->threads[core_id].wait_gate.active;
}

/* Check FIFO status */
bool tensix_cop_fifo_empty(tensix_cop_t *cop, int core_id)
{
    if (!cop || core_id < 0 || core_id >= TENSIX_NUM_CORES)
        return true;

    return tensix_fifo_is_empty(&cop->threads[core_id].insn_fifo);
}

bool tensix_cop_fifo_full(tensix_cop_t *cop, int core_id)
{
    if (!cop || core_id < 0 || core_id >= TENSIX_NUM_CORES)
        return false;

    return tensix_fifo_is_full(&cop->threads[core_id].insn_fifo);
}

int tensix_cop_fifo_count(tensix_cop_t *cop, int core_id)
{
    if (!cop || core_id < 0 || core_id >= TENSIX_NUM_CORES)
        return 0;

    return tensix_fifo_count(&cop->threads[core_id].insn_fifo);
}

/* ============================================================================
 * MOP Expander (lazy state machine - generates one instruction at a time)
 * ============================================================================ */

/* Check if instruction is a NOP (only opcode 0x02 counts) */
static bool mop_is_nop(uint32_t insn)
{
    return ((insn >> 24) & 0xFF) == OPCODE_NOP;
}

/* Generate next instruction for Template 0 (mask-based selection).
 * Returns true with *out_insn set, or false when expansion is done. */
static bool mop_generate_next_t0(tensix_cop_t *cop, int core_id,
                                 uint32_t *out_insn)
{
    tensix_thread_cop_t *thread = &cop->threads[core_id];
    mop_expand_state_t *st = &thread->mop_state;
    uint32_t *cfg = thread->mop_cfg;

    uint32_t flags   = cfg[1];
    uint32_t insn_b  = cfg[2];
    uint32_t insn_a0 = cfg[3];
    uint32_t insn_a1 = cfg[4];
    uint32_t insn_a2 = cfg[5];
    uint32_t insn_a3 = cfg[6];
    uint32_t skip_a0 = cfg[7];
    uint32_t skip_b  = cfg[8];

    bool has_b    = (flags & 1) != 0;
    bool has_a123 = (flags & 2) != 0;

    /* If we're in the middle of emitting sub-instructions for current iter */
    if (st->tmpl0_sub < st->tmpl0_num_sub) {
        *out_insn = st->tmpl0_sub_insns[st->tmpl0_sub++];
        return true;
    }

    /* Advance to next iteration */
    if (st->tmpl0_i >= st->tmpl0_count) {
        st->active = false;
        return false;
    }

    /* Build sub-instruction list for current iteration */
    st->tmpl0_num_sub = 0;
    st->tmpl0_sub = 0;

    if ((st->tmpl0_mask & 1) == 0) {
        /* zmask bit = 0: execute actual instructions */
        st->tmpl0_sub_insns[st->tmpl0_num_sub++] = insn_a0;
        if (has_a123) {
            st->tmpl0_sub_insns[st->tmpl0_num_sub++] = insn_a1;
            st->tmpl0_sub_insns[st->tmpl0_num_sub++] = insn_a2;
            st->tmpl0_sub_insns[st->tmpl0_num_sub++] = insn_a3;
        }
        if (has_b) {
            st->tmpl0_sub_insns[st->tmpl0_num_sub++] = insn_b;
        }
    } else {
        /* zmask bit = 1: execute skip instructions */
        st->tmpl0_sub_insns[st->tmpl0_num_sub++] = skip_a0;
        if (has_b) {
            st->tmpl0_sub_insns[st->tmpl0_num_sub++] = skip_b;
        }
    }

    st->tmpl0_mask >>= 1;
    st->tmpl0_i++;

    /* Emit first sub-instruction */
    *out_insn = st->tmpl0_sub_insns[st->tmpl0_sub++];
    return true;
}

/* Generate next instruction for Template 1 (nested loop).
 * Returns true with *out_insn set, or false when expansion is done. */
static bool mop_generate_next_t1(tensix_cop_t *cop, int core_id,
                                 uint32_t *out_insn)
{
    tensix_thread_cop_t *thread = &cop->threads[core_id];
    mop_expand_state_t *st = &thread->mop_state;
    uint32_t *cfg = thread->mop_cfg;

    uint32_t start_op   = cfg[2];
    uint32_t end_op0    = cfg[3];
    uint32_t end_op1    = cfg[4];
    uint32_t loop0_last = cfg[7];
    uint32_t loop1_last = cfg[8];

    for (;;) {
        /* Check if all outer iterations are done */
        if (st->tmpl1_j >= st->tmpl1_outer) {
            st->active = false;
            return false;
        }

        switch (st->tmpl1_phase) {
        case 0: /* START: emit start_op if not NOP */
            st->tmpl1_phase = 1;
            st->tmpl1_i = 0;
            if (!mop_is_nop(start_op)) {
                *out_insn = start_op;
                return true;
            }
            break; /* start_op is NOP, skip to INNER */

        case 1: /* INNER LOOP */
            if (st->tmpl1_i < st->tmpl1_inner) {
                uint32_t insn;
                if (st->tmpl1_i != st->tmpl1_inner - 1) {
                    insn = st->tmpl1_loop_op;
                } else if (st->tmpl1_j != st->tmpl1_outer - 1) {
                    insn = loop1_last;
                } else {
                    insn = loop0_last;
                }
                st->tmpl1_loop_op ^= st->tmpl1_loop_op_flip;
                st->tmpl1_i++;
                *out_insn = insn;
                return true;
            }
            st->tmpl1_phase = 2;
            break; /* inner loop done, go to END0 */

        case 2: /* END0: emit end_op0 if not NOP */
            st->tmpl1_phase = 3;
            if (!mop_is_nop(end_op0)) {
                *out_insn = end_op0;
                return true;
            }
            break; /* end_op0 is NOP, skip to END1 */

        case 3: /* END1: emit end_op1 if not NOP, then advance outer */
            st->tmpl1_j++;
            st->tmpl1_phase = 0; /* next outer iteration starts at START */
            if (!mop_is_nop(end_op1)) {
                *out_insn = end_op1;
                return true;
            }
            break; /* end_op1 is NOP, continue to next outer (or done) */
        }
    }
}

/* Generate the next MOP-expanded instruction.
 * Returns true with *out_insn set, or false when expansion is complete. */
static bool mop_generate_next(tensix_cop_t *cop, int core_id,
                              uint32_t *out_insn)
{
    mop_expand_state_t *st = &cop->threads[core_id].mop_state;

    if (!st->active)
        return false;

    if (st->tmpl == 0)
        return mop_generate_next_t0(cop, core_id, out_insn);
    else
        return mop_generate_next_t1(cop, core_id, out_insn);
}

/* Continue executing MOP-expanded instructions one at a time.
 * Each instruction is passed to tensix_cop_step via has_current_insn,
 * which handles wait gate, dvalid checks, and execution.
 * Returns true when all MOP instructions are done, false if blocked. */
static bool mop_continue_execution(tensix_cop_t *cop, int core_id)
{
    tensix_thread_cop_t *thread = &cop->threads[core_id];

    /* If there's a pending instruction from last attempt, retry it */
    if (thread->has_current_insn) {
        if (!tensix_cop_step(cop, core_id)) {
            return false;  /* Still blocked */
        }
    }

    /* Generate and execute instructions one by one */
    uint32_t next_insn;
    while (mop_generate_next(cop, core_id, &next_insn)) {
        /* Set as current instruction and execute through standard path */
        thread->current_insn = next_insn;
        thread->has_current_insn = true;
        if (!tensix_cop_step(cop, core_id)) {
            return false;  /* Blocked; instruction saved in has_current_insn */
        }
    }

    /* All MOP instructions generated and executed */
    thread->mop_state.active = false;
    thread->has_direct_insn = false;
    thread->state = THREAD_STATE_RUNNING;
    thread->insn_executed++;
    return true;
}

/* Main MOP expand entry point: initialize lazy expansion state.
 * No instructions are pushed to FIFO; they are generated on demand
 * by mop_continue_execution / mop_generate_next. */
void tensix_cop_mop_expand(tensix_cop_t *cop, int core_id, uint32_t param)
{
    tensix_thread_cop_t *thread = &cop->threads[core_id];
    mop_expand_state_t *st = &thread->mop_state;
    uint32_t *cfg = thread->mop_cfg;

    uint32_t tmpl = (param >> 23) & 0x1;

    memset(st, 0, sizeof(*st));
    st->active = true;
    st->tmpl = tmpl;

    if (tmpl == 0) {
        uint32_t count1 = (param >> 16) & 0x7F;
        uint32_t mask_lo = param & 0xFFFF;
        uint32_t zmask_hi16 = thread->zmask_hi16;
        uint32_t mask = (zmask_hi16 << 16) | mask_lo;

        st->tmpl0_mask = mask;
        st->tmpl0_i = 0;
        st->tmpl0_count = count1 + 1;
        st->tmpl0_sub = 0;
        st->tmpl0_num_sub = 0;
    } else {
        uint32_t outer_count = cfg[0] & 127;
        uint32_t inner_count = cfg[1] & 127;
        uint32_t loop_op     = cfg[5];
        uint32_t loop_op1    = cfg[6];
        uint32_t start_op    = cfg[2];
        uint32_t end_op0     = cfg[3];

        uint32_t loop_op_flip;
        if (mop_is_nop(loop_op1)) {
            loop_op_flip = 0;
        } else {
            loop_op_flip = loop_op ^ loop_op1;
            inner_count *= 2;
        }

        /* Hardware bug workaround */
        if (outer_count == 1 && mop_is_nop(start_op) &&
            inner_count == 0 && !mop_is_nop(end_op0)) {
            outer_count += 128;
        }

        st->tmpl1_j = 0;
        st->tmpl1_i = 0;
        st->tmpl1_outer = outer_count;
        st->tmpl1_inner = inner_count;
        st->tmpl1_phase = 0;
        st->tmpl1_loop_op = loop_op;
        st->tmpl1_loop_op_flip = loop_op_flip;
    }
}

/* Execute one instruction (internal function)
 * Returns true if instruction completed, false if not yet done
 */
bool tensix_cop_execute_insn(tensix_cop_t *cop, int core_id, uint32_t insn)
{
    tensix_t *core = cop->core;

    /* Pass core_id as tid to avoid race conditions on shared thread_id */
    return tensix_execute_insn(core, insn, core_id);
}
