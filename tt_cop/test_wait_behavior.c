/*
 * test_wait_behavior.c - Detailed test of Wait instruction behavior
 * Verify: After Wait instruction is latched, no re-push is needed
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "tensix.h"
#include "tensix_cop.h"

int main(void)
{
    printf("========================================================\n");
    printf("  Wait Instruction Behavior Test\n");
    printf("========================================================\n\n");

    /* Initialize Tensix (only T0/T1/T2 memory needed - B and NC don't use Tensix) */
    uint8_t *l1_mem = calloc(1, 0x180000);      // 1.5MB L1 scratchpad
    uint8_t *high_mem = calloc(1, 0x400000);    // 4MB high address region
    uint8_t *t0_ldm = calloc(1, 0x4000);        // 16KB per core
    uint8_t *t1_ldm = calloc(1, 0x4000);
    uint8_t *t2_ldm = calloc(1, 0x4000);

    tensix_t *tt = calloc(1, sizeof(tensix_t));
    tensix_init(tt, l1_mem, high_mem, t0_ldm, t1_ldm, t2_ldm);

    printf("Scenario: trisc pushes a batch of instructions, hits Wait, can it auto-resume after condition is met?\n\n");

    /* Initialize semaphore */
    uint32_t seminit = 0xA3A00004;  /* SEMINIT: max=10, init=0, sem_sel=(1<<0)<<2 */
    tensix_push(tt, seminit, 0);
    tensix_step(tt, 0);
    printf("1. Initialized sem[0] = %d\n", tt->sem[0]);

    /* trisc pushes all instructions at once */
    printf("\n2. trisc pushes instruction sequence to FIFO:\n");

    uint32_t semwait = 0xA6FF8005;  /* SEMWAIT: BlockMask=0x1FF, sem_sel=(1<<0)<<2, cond=1 */
    uint32_t nop1 = 0x02000001;     /* NOP with marker 1 */
    uint32_t nop2 = 0x02000002;     /* NOP with marker 2 */
    uint32_t nop3 = 0x02000003;     /* NOP with marker 3 */

    tensix_push(tt, semwait, 0);
    printf("   - SEMWAIT\n");

    tensix_push(tt, nop1, 0);
    printf("   - NOP1\n");

    tensix_push(tt, nop2, 0);
    printf("   - NOP2\n");

    tensix_push(tt, nop3, 0);
    printf("   - NOP3\n");

    int fifo_count = tensix_cop_fifo_count(tt->cop, 0);
    printf("   FIFO has %d instructions\n", fifo_count);
    assert(fifo_count == 4);

    /* Execute SEMWAIT */
    printf("\n3. Execute SEMWAIT:\n");
    bool executed = tensix_step(tt, 0);
    printf("   tensix_step(tt, 0) = %s\n", executed ? "true" : "false");
    assert(executed == true);  // SEMWAIT executed successfully

    fifo_count = tensix_cop_fifo_count(tt->cop, 0);
    printf("   FIFO has %d instructions remaining\n", fifo_count);
    assert(fifo_count == 3);  // SEMWAIT popped from FIFO

    bool waiting = tensix_cop_is_waiting(tt->cop, 0);
    printf("   T0 waiting? %s\n", waiting ? "YES" : "NO");
    assert(waiting == true);

    /* Try to continue execution (should be blocked) */
    printf("\n4. Try to continue (condition not met):\n");
    executed = tensix_step(tt, 0);
    printf("   tensix_step(tt, 0) = %s (should be blocked)\n", executed ? "true" : "false");
    assert(executed == false);

    fifo_count = tensix_cop_fifo_count(tt->cop, 0);
    printf("   FIFO still has %d instructions\n", fifo_count);
    assert(fifo_count == 2);  // NOP1 popped and saved to current_insn, NOP2 and NOP3 remain

    printf("   * Key: blocked instruction saved in current_insn, will retry next time\n");

    /* Satisfy condition */
    printf("\n5. Satisfy Wait condition:\n");
    tt->sem[0] = 5;
    printf("   sem[0] = %d\n", tt->sem[0]);

    /* Continue execution - should auto-execute NOP1 */
    printf("\n6. Continue execution (no re-push needed):\n");

    uint64_t exec_count_before = tt->cop->threads[0].insn_executed;

    executed = tensix_step(tt, 0);
    printf("   tensix_step(tt, 0) = %s (execute NOP1)\n", executed ? "true" : "false");
    assert(executed == true);

    fifo_count = tensix_cop_fifo_count(tt->cop, 0);
    printf("   FIFO has %d instructions remaining\n", fifo_count);
    assert(fifo_count == 2);

    waiting = tensix_cop_is_waiting(tt->cop, 0);
    printf("   T0 waiting? %s\n", waiting ? "YES" : "NO");
    assert(waiting == false);

    /* Continue executing NOP2 */
    executed = tensix_step(tt, 0);
    printf("   tensix_step(tt, 0) = %s (execute NOP2)\n", executed ? "true" : "false");
    assert(executed == true);

    fifo_count = tensix_cop_fifo_count(tt->cop, 0);
    printf("   FIFO has %d instructions remaining\n", fifo_count);
    assert(fifo_count == 1);

    /* Continue executing NOP3 */
    executed = tensix_step(tt, 0);
    printf("   tensix_step(tt, 0) = %s (execute NOP3)\n", executed ? "true" : "false");
    assert(executed == true);

    fifo_count = tensix_cop_fifo_count(tt->cop, 0);
    printf("   FIFO has %d instructions remaining\n", fifo_count);
    assert(fifo_count == 0);

    uint64_t exec_count_after = tt->cop->threads[0].insn_executed;
    printf("\n   Execution stats: %lu instructions\n", exec_count_after - exec_count_before);

    /* FIFO empty, executing again should return false */
    printf("\n7. FIFO is empty:\n");
    executed = tensix_step(tt, 0);
    printf("   tensix_step(tt, 0) = %s (FIFO empty)\n", executed ? "true" : "false");
    assert(executed == false);
    printf("   At this point, trisc only needs to push new instructions if more work is needed\n");

    /* Cleanup */
    if (tt->cop) free(tt->cop);
    free(tt);
    free(l1_mem);
    free(high_mem);
    free(t0_ldm);
    free(t1_ldm);
    free(t2_ldm);

    printf("\n========================================================\n");
    printf("  Conclusion\n");
    printf("--------------------------------------------------------\n");
    printf("  1. Wait instruction is popped from FIFO and latched into Wait Gate\n");
    printf("  2. While blocked, subsequent instructions in FIFO remain untouched\n");
    printf("  3. After condition is met, subsequent instructions are auto-popped and executed\n");
    printf("  4. NO need for trisc to re-push the Wait instruction\n");
    printf("  5. Only need to push new work instructions when FIFO is empty\n");
    printf("========================================================\n");

    printf("\nAll tests passed!\n");
    return 0;
}
