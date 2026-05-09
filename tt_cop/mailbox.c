/*
 * Mailbox FIFO implementation for Tenstorrent Tensix tile.
 *
 * 16 FIFOs: 4 cores (B, T0, T1, T2) x 4 targets.
 * Routing per ISA Mailboxes.md:
 *   Write to MAILBOX_N by core_S  => FIFO[S][N]  (S sends to N)
 *   Read from MAILBOX_N by core_R => FIFO[N][R]  (R receives from N)
 *   Exception: when S == N (diagonal), both read/write go to FIFO[S][S]
 *
 * Blocking semantics (ISA):
 *   Read pop from empty FIFO:  wait (stall core, retry next cycle)
 *   Write push to full FIFO:   wait (stall core, retry next cycle)
 *   Read query (addr & 4):     always immediate, returns 0 or 1
 *
 * In coroutine model, "stall" = set mailbox_stall flag, caller rewinds PC and yields.
 */

#include <stdio.h>
#include "mailbox.h"

#define MAILBOX_ADDR_BASE  0xFFEC0000
#define MAILBOX_ADDR_END   0xFFEC3FFF

/* Get mailbox target index (0-3) from address */
static inline int mailbox_target(uint32_t addr) {
    return (addr - MAILBOX_ADDR_BASE) >> 12;
}

static inline bool in_mailbox_range(uint32_t addr) {
    return addr >= MAILBOX_ADDR_BASE && addr <= MAILBOX_ADDR_END;
}

/* Try push. Returns true on success, false if FIFO full (caller should stall). */
static bool fifo_push(tensix_t *tt, int src, int dst, uint32_t value) {
    int count = tt->mailbox_count[src][dst];
    if (count >= MAILBOX_FIFO_DEPTH) {
        return false;
    }
    int idx = (tt->mailbox_head[src][dst] + count) % MAILBOX_FIFO_DEPTH;
    tt->mailbox_fifo[src][dst][idx] = value;
    tt->mailbox_count[src][dst] = count + 1;
    return true;
}

/* Try pop. Returns true on success, false if FIFO empty (caller should stall). */
static bool fifo_pop(tensix_t *tt, int src, int dst, uint32_t *value) {
    int count = tt->mailbox_count[src][dst];
    if (count <= 0) {
        return false;
    }
    int head = tt->mailbox_head[src][dst];
    *value = tt->mailbox_fifo[src][dst][head];
    tt->mailbox_head[src][dst] = (head + 1) % MAILBOX_FIFO_DEPTH;
    tt->mailbox_count[src][dst] = count - 1;
    return true;
}

bool mailbox_write(tensix_t *tt, int core_id, uint32_t addr, uint32_t value) {
    if (!in_mailbox_range(addr))
        return false;

    int src = mailbox_core_index(core_id);
    if (src < 0) {
        fprintf(stderr, "[MAILBOX] WARN: NCRISC (core_id=%d) attempted mailbox write, ignored\n", core_id);
        return true;
    }

    int dst = mailbox_target(addr);
    if (!fifo_push(tt, src, dst, value)) {
        /* FIFO full — firmware bug: sw to mailbox should never fill FIFO */
        fprintf(stderr, "[MAILBOX] ASSERT: core %d write FIFO[%d][%d] full — firmware error\n", core_id, src, dst);
        /* Do not stall: sw must never block */
    }
    return true;
}

bool mailbox_read(tensix_t *tt, int core_id, uint32_t addr, uint32_t *result) {
    if (!in_mailbox_range(addr))
        return false;

    int reader = mailbox_core_index(core_id);
    if (reader < 0) {
        fprintf(stderr, "[MAILBOX] WARN: NCRISC (core_id=%d) attempted mailbox read, ignored\n", core_id);
        *result = 0;
        return true;
    }

    int mbox = mailbox_target(addr);

    /* Routing: reader reads MAILBOX_N => FIFO[N][reader] (N sent to reader) */
    int src = mbox;
    int dst = reader;

    if (addr & 4) {
        /* Non-blocking query: return 1 if data available, 0 otherwise */
        *result = (tt->mailbox_count[src][dst] > 0) ? 1 : 0;
    } else {
        /* Blocking pop: stall if FIFO empty (per ISA: wait until data arrives) */
        if (!fifo_pop(tt, src, dst, result)) {
            *result = 0;
            tt->mailbox_stall[reader] = true;
        }
    }
    return true;
}

bool mailbox_process(tensix_t *tt, int core_id) {
    int mci = mailbox_core_index(core_id);
    if (mci < 0)
        return false;
    if (tt->mailbox_stall[mci]) {
        tt->mailbox_stall[mci] = false;
        return true;
    }
    return false;
}
