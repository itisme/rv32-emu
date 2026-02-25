/*
 * tensix_cop.h - Tensix Coprocessor state machine and FIFO management
 *
 * This extends the existing tensix.h with:
 * - Instruction FIFOs for T0/T1/T2 threads
 * - State machine for concurrent execution
 * - Wait gates and synchronization
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "tensix.h"

#ifdef __cplusplus
extern "C" {
#endif

/* FIFO configuration */
#define TENSIX_FIFO_SIZE 32

/* Core ID enumeration */
typedef enum {
    TENSIX_CORE_T0 = 0,
    TENSIX_CORE_T1 = 1,
    TENSIX_CORE_T2 = 2,
    TENSIX_NUM_CORES = 3
} tensix_core_id_t;

/* Tensix thread instruction FIFO */
typedef struct tensix_fifo tensix_fifo_t;
struct tensix_fifo {
    uint32_t buffer[TENSIX_FIFO_SIZE];
    uint32_t head;    /* Read index */
    uint32_t tail;    /* Write index */
    uint32_t count;   /* Number of items in FIFO */
};

/* Wait gate state (per ISA documentation) */
typedef struct tensix_wait_gate tensix_wait_gate_t;
struct tensix_wait_gate {
    bool active;              /* Whether Wait Gate is active */
    uint32_t opcode;          /* STALLWAIT/SEMWAIT/STREAMWAIT */
    uint32_t condition_mask;  /* Condition mask */
    uint32_t block_mask;      /* Block mask */
    uint32_t semaphore_mask;  /* Semaphore mask (SEMWAIT) */
    uint32_t target_value;    /* Target value (STREAMWAIT) */
    uint32_t stream_select;   /* Stream select (STREAMWAIT) */
};

/* Thread execution state */
typedef enum {
    THREAD_STATE_IDLE,      /* No instructions to execute */
    THREAD_STATE_RUNNING,   /* Actively executing */
    THREAD_STATE_WAITING,   /* Waiting on a gate/semaphore */
    THREAD_STATE_STALLED    /* Stalled on resource conflict */
} tensix_thread_state_t;

/* MOP expansion state machine (lazy iterator, generates one instruction at a time) */
typedef struct mop_expand_state mop_expand_state_t;
struct mop_expand_state {
    bool active;             /* MOP expansion in progress */
    uint32_t tmpl;           /* Template 0 or 1 */

    /* Template 0 state (mask-based selection) */
    uint32_t tmpl0_mask;        /* Current mask (shifted each iteration) */
    uint32_t tmpl0_i;           /* Current iteration index */
    uint32_t tmpl0_count;       /* Total iterations (count1 + 1) */
    uint32_t tmpl0_sub;         /* Sub-phase index within current iteration */
    uint32_t tmpl0_num_sub;     /* Number of sub-instructions for current iteration */
    uint32_t tmpl0_sub_insns[5]; /* Sub-instructions buffer (max: a0,a1,a2,a3,b) */

    /* Template 1 state (nested loop) */
    uint32_t tmpl1_j;           /* Outer loop index */
    uint32_t tmpl1_i;           /* Inner loop index */
    uint32_t tmpl1_outer;       /* Outer count */
    uint32_t tmpl1_inner;       /* Inner count */
    uint32_t tmpl1_phase;       /* Phase: 0=START, 1=INNER, 2=END0, 3=END1 */
    uint32_t tmpl1_loop_op;     /* Current loop_op (XOR'd) */
    uint32_t tmpl1_loop_op_flip; /* XOR flip value */
};

/* Per-thread coprocessor state */
typedef struct tensix_thread_cop tensix_thread_cop_t;
struct tensix_thread_cop {
    /* Instruction FIFO */
    tensix_fifo_t insn_fifo;

    /* Thread state */
    tensix_thread_state_t state;
    tensix_wait_gate_t wait_gate;

    /* Current instruction being executed (from FIFO) */
    uint32_t current_insn;
    bool has_current_insn;

    /* Direct instruction (from do_ttinsn, not FIFO) */
    uint32_t direct_insn;
    bool has_direct_insn;

    /* Stall support */
    uint32_t stall_cycles;

    /* MOP expander state (per-thread) */
    uint32_t mop_cfg[9];       /* MOP config registers (written via 0xFFB80000) */
    uint32_t zmask_hi16;       /* MOP_CFG instruction sets high 16 bits of mask */
    mop_expand_state_t mop_state; /* Lazy MOP expansion state machine */

    /* Statistics */
    uint64_t insn_executed;
    uint64_t cycles_idle;
    uint64_t cycles_waiting;
};

/* Tensix Coprocessor state machine */
typedef struct tensix_cop tensix_cop_t;
struct tensix_cop {
    /* Three threads T0, T1, T2 */
    tensix_thread_cop_t threads[3];

    /* Shared Tensix state (from original tensix.h) */
    tensix_t *core;

    /* Global coprocessor state */
    bool halted;
    uint64_t total_cycles;

    /* Memory access callbacks */
    void *mem_ctx;
    uint32_t (*mem_read_fn)(void *ctx, uint32_t addr, uint32_t size);
    void (*mem_write_fn)(void *ctx, uint32_t addr, uint32_t data, uint32_t size);

};

/* FIFO operations */
void tensix_fifo_init(tensix_fifo_t *fifo);
bool tensix_fifo_is_empty(const tensix_fifo_t *fifo);
bool tensix_fifo_is_full(const tensix_fifo_t *fifo);
bool tensix_fifo_push(tensix_fifo_t *fifo, uint32_t insn);
bool tensix_fifo_pop(tensix_fifo_t *fifo, uint32_t *insn);
uint32_t tensix_fifo_count(const tensix_fifo_t *fifo);

/* Coprocessor initialization and control */
void tensix_cop_init(tensix_cop_t *cop, tensix_t *core);
void tensix_cop_reset(tensix_cop_t *cop);
void tensix_cop_halt(tensix_cop_t *cop);
void tensix_cop_resume(tensix_cop_t *cop);

/* Instruction push interface (called from RISCV cores)
 * Returns: true = pushed successfully, false = FIFO full
 */
bool tensix_cop_push(tensix_cop_t *cop, int core_id, uint32_t insn);

/* Direct instruction interface (for do_ttinsn - inline TT instructions in code)
 * These instructions bypass the FIFO, execute directly, with blocking support
 */
void tensix_cop_set_direct(tensix_cop_t *cop, int core_id, uint32_t insn);
bool tensix_cop_step_direct(tensix_cop_t *cop, int core_id);

/* Convenience functions (backward compatible) */
bool tensix_cop_push_t0(tensix_cop_t *cop, uint32_t insn);
bool tensix_cop_push_t1(tensix_cop_t *cop, uint32_t insn);
bool tensix_cop_push_t2(tensix_cop_t *cop, uint32_t insn);

/* Execute one instruction for a given core's thread
 *
 * @param cop      - Tensix coprocessor
 * @param core_id  - Core ID (0=T0, 1=T1, 2=T2)
 *
 * @return true  - An instruction was executed this cycle (including Wait instructions)
 *         false - No instruction executed this cycle (Wait Gate blocked or FIFO empty)
 *
 * Note: Wait instructions return true because they are popped from FIFO and latched into Wait Gate
 */
bool tensix_cop_step(tensix_cop_t *cop, int core_id);

/* Execute one instruction for all cores' threads
 * @return Number of cores that executed an instruction (0-3)
 */
int tensix_cop_step_all(tensix_cop_t *cop);

/* Execute a single core until FIFO empty or Wait encountered
 * @param max_insns - Maximum instructions to execute (prevents infinite loop)
 * @return Number of instructions actually executed
 */
int tensix_cop_run_until_wait(tensix_cop_t *cop, int core_id, int max_insns);

/* Execute all cores until all FIFOs empty or Wait encountered */
int tensix_cop_run_until_wait_all(tensix_cop_t *cop, int max_insns);

/* Check if a core is blocked by Wait Gate */
bool tensix_cop_is_waiting(tensix_cop_t *cop, int core_id);

/* Check FIFO status */
bool tensix_cop_fifo_empty(tensix_cop_t *cop, int core_id);
bool tensix_cop_fifo_full(tensix_cop_t *cop, int core_id);
int tensix_cop_fifo_count(tensix_cop_t *cop, int core_id);

/* Execute one instruction (internal function)
 * Returns true if instruction completed, false if not yet done (e.g. MOP expansion in progress)
 */
bool tensix_cop_execute_insn(tensix_cop_t *cop, int core_id, uint32_t insn);

/* MOP expander: initialize lazy expansion state (no FIFO push) */
void tensix_cop_mop_expand(tensix_cop_t *cop, int core_id, uint32_t param);

/* Set memory access callbacks */
void tensix_cop_set_memory(tensix_cop_t *cop, void *ctx,
                           uint32_t (*read_fn)(void *, uint32_t, uint32_t),
                           void (*write_fn)(void *, uint32_t, uint32_t, uint32_t));

/* Helper functions for instruction decode */
uint32_t tensix_insn_opcode(uint32_t insn);
uint32_t tensix_insn_param(uint32_t insn);

#ifdef __cplusplus
}
#endif
