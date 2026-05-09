#ifndef __MAILBOX_H__
#define __MAILBOX_H__

#include "tensix.h"

/* Convert rv32-emu core_id (-1=B, 0=T0, 1=T1, 2=T2) to mailbox core index (0=B, 1=T0, 2=T1, 3=T2).
 * Returns -1 for NCRISC or invalid core_id. */
static inline int mailbox_core_index(int core_id) {
    if (core_id == -1) return 0;       /* BRISC */
    if (core_id >= 0 && core_id <= 2)  /* TRISC0-2 */
        return core_id + 1;
    return -1;                         /* NCRISC or invalid */
}

/* Handle mailbox write (sw to 0xFFEC_x000).
 * Routes based on ISA mailbox table: FIFO[src_core][dst_core].
 * Returns true if addr is in mailbox range (0xFFEC0000-0xFFEC3FFF). */
bool mailbox_write(tensix_t *tt, int core_id, uint32_t addr, uint32_t value);

/* Handle mailbox read (lw from 0xFFEC_x000 or 0xFFEC_x004).
 * addr & 4 == 0: blocking pop (asserts if FIFO empty)
 * addr & 4 != 0: non-blocking query (returns 0 or 1)
 * Returns true if addr is in mailbox range. */
bool mailbox_read(tensix_t *tt, int core_id, uint32_t addr, uint32_t *result);

/* Check and clear stall for a core. Returns true if the core was stalled
 * (caller should rewind PC and yield). */
bool mailbox_process(tensix_t *tt, int core_id);

#endif /* __MAILBOX_H__ */
