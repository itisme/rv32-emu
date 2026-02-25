/*
 * test_ttcop.c - Test program for Tensix coprocessor library
 * Tests the new core_id-based interface and verifies instruction stream independence
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "tensix.h"
#include "tensix_cop.h"

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) \
    printf("\n=== Test: %s ===\n", name)

#define TEST_PASS() \
    do { \
        tests_passed++; \
        printf("✓ PASS\n"); \
    } while(0)

#define TEST_FAIL(msg) \
    do { \
        tests_failed++; \
        printf("✗ FAIL: %s\n", msg); \
    } while(0)

#define ASSERT_EQ(expected, actual, msg) \
    do { \
        if ((expected) != (actual)) { \
            printf("  Expected %d, got %d: %s\n", (int)(expected), (int)(actual), msg); \
            TEST_FAIL(msg); \
            return; \
        } \
    } while(0)

/* Test 1: Basic instruction execution on single core */
void test_single_core_execution(tensix_t *tt)
{
    TEST_START("Single core execution");

    tensix_reset(tt);

    /* Push 3 NOP instructions to T0 */
    uint32_t nop_insn = 0x02000000;  /* TTNOP */

    for (int i = 0; i < 3; i++) {
        assert(tensix_push(tt, nop_insn, 0));
    }

    /* Execute them one by one on T0 */
    for (int i = 0; i < 3; i++) {
        bool executed = tensix_step(tt, 0);
        ASSERT_EQ(true, executed, "Should execute instruction");
    }

    /* Check T0 executed 3 instructions */
    ASSERT_EQ(3, tt->cop->threads[0].insn_executed, "T0 should execute 3 instructions");

    /* Check T1 and T2 executed nothing */
    ASSERT_EQ(0, tt->cop->threads[1].insn_executed, "T1 should execute 0 instructions");
    ASSERT_EQ(0, tt->cop->threads[2].insn_executed, "T2 should execute 0 instructions");

    TEST_PASS();
}

/* Test 2: Independent instruction streams */
void test_independent_streams(tensix_t *tt)
{
    TEST_START("Independent instruction streams");

    tensix_reset(tt);

    /* Push different numbers of NOPs to each core */
    uint32_t nop_insn = 0x02000000;

    /* T0: 2 instructions */
    tensix_push(tt, nop_insn, 0);
    tensix_push(tt, nop_insn, 0);

    /* T1: 3 instructions */
    tensix_push(tt, nop_insn, 1);
    tensix_push(tt, nop_insn, 1);
    tensix_push(tt, nop_insn, 1);

    /* T2: 1 instruction */
    tensix_push(tt, nop_insn, 2);

    printf("  Pushed: T0=2, T1=3, T2=1\n");

    /* Execute T0 only */
    printf("  Executing T0 only...\n");
    for (int i = 0; i < 2; i++) {
        bool executed = tensix_step(tt, 0);
        ASSERT_EQ(true, executed, "T0 should execute");
    }

    /* Check execution counts */
    ASSERT_EQ(2, tt->cop->threads[0].insn_executed, "T0 executed count");
    ASSERT_EQ(0, tt->cop->threads[1].insn_executed, "T1 not affected");
    ASSERT_EQ(0, tt->cop->threads[2].insn_executed, "T2 not affected");

    /* Execute T1 only */
    printf("  Executing T1 only...\n");
    for (int i = 0; i < 3; i++) {
        bool executed = tensix_step(tt, 1);
        ASSERT_EQ(true, executed, "T1 should execute");
    }

    ASSERT_EQ(2, tt->cop->threads[0].insn_executed, "T0 still at 2");
    ASSERT_EQ(3, tt->cop->threads[1].insn_executed, "T1 executed count");
    ASSERT_EQ(0, tt->cop->threads[2].insn_executed, "T2 still at 0");

    /* Execute T2 only */
    printf("  Executing T2 only...\n");
    bool executed = tensix_step(tt, 2);
    ASSERT_EQ(true, executed, "T2 should execute");

    ASSERT_EQ(2, tt->cop->threads[0].insn_executed, "T0 unchanged");
    ASSERT_EQ(3, tt->cop->threads[1].insn_executed, "T1 unchanged");
    ASSERT_EQ(1, tt->cop->threads[2].insn_executed, "T2 executed count");

    TEST_PASS();
}

/* Test 3: Wait instruction isolation */
void test_wait_isolation(tensix_t *tt)
{
    TEST_START("Wait instruction isolation");

    tensix_reset(tt);

    /* Initialize semaphore on T0 */
    uint32_t seminit = 0xA3000004;  /* TTSEMINIT: max=0, init=0, sem_sel=(1<<0)<<2 */
    tensix_push(tt, seminit, 0);
    tensix_step(tt, 0);

    printf("  Initialized semaphore 0 on T0 (value=%d)\n", tt->sem[0]);

    /* Push SEMWAIT to T0 (will block because sem[0] = 0)
     * SEMWAIT encoding (ckernel_ops.h):
     * - Bits [23:15] = stall_res (BlockMask) = 0x1FF (all bits, to block NOP)
     * - Bits [14:2]  = sem_sel = (1<<0)<<2 = 0x04 (select sem[0])
     * - Bits [1:0]   = wait_cond = 1 (C0: wait until non-zero)
     * Note: NOP requires BlockMask=0x1FF to be blocked (per STALLWAIT.md)
     */
    uint32_t semwait = 0xA6FF8005;  /* TTSEMWAIT: (0x1FF<<15) | (1<<2) | 1 */
    tensix_push(tt, semwait, 0);

    /* Push NOP to T0 (will execute after wait is satisfied) */
    uint32_t nop = 0x02000000;
    tensix_push(tt, nop, 0);

    /* Push NOP to T1 and T2 */
    tensix_push(tt, nop, 1);
    tensix_push(tt, nop, 2);

    /* Execute T0: SEMWAIT should execute and lock */
    printf("  Executing T0 SEMWAIT...\n");
    bool executed = tensix_step(tt, 0);
    ASSERT_EQ(true, executed, "SEMWAIT should execute");
    ASSERT_EQ(true, tensix_cop_is_waiting(tt->cop, 0), "T0 should be waiting");

    /* Try to execute T0 again: should be blocked */
    printf("  Trying to execute T0 again (should block)...\n");
    executed = tensix_step(tt, 0);
    ASSERT_EQ(false, executed, "T0 should be blocked");
    ASSERT_EQ(true, tensix_cop_is_waiting(tt->cop, 0), "T0 still waiting");

    /* Execute T1: should work normally */
    printf("  Executing T1 (should not be affected)...\n");
    executed = tensix_step(tt, 1);
    ASSERT_EQ(true, executed, "T1 should execute normally");
    ASSERT_EQ(false, tensix_cop_is_waiting(tt->cop, 1), "T1 should not be waiting");

    /* Execute T2: should work normally */
    printf("  Executing T2 (should not be affected)...\n");
    executed = tensix_step(tt, 2);
    ASSERT_EQ(true, executed, "T2 should execute normally");
    ASSERT_EQ(false, tensix_cop_is_waiting(tt->cop, 2), "T2 should not be waiting");

    /* Verify only T0 is blocked */
    ASSERT_EQ(true, tensix_cop_is_waiting(tt->cop, 0), "Only T0 waiting");
    ASSERT_EQ(false, tensix_cop_is_waiting(tt->cop, 1), "T1 not waiting");
    ASSERT_EQ(false, tensix_cop_is_waiting(tt->cop, 2), "T2 not waiting");

    printf("  ✓ T0 blocked, T1 and T2 unaffected\n");

    /* Now satisfy T0's wait condition */
    printf("  Satisfying T0's wait condition (sem[0] = 1)...\n");
    tt->sem[0] = 1;

    /* T0 should now be able to proceed */
    executed = tensix_step(tt, 0);
    ASSERT_EQ(true, executed, "T0 should proceed after condition met");
    ASSERT_EQ(false, tensix_cop_is_waiting(tt->cop, 0), "T0 no longer waiting");

    TEST_PASS();
}

/* Test 4: Step all cores */
void test_step_all(tensix_t *tt)
{
    TEST_START("Step all cores");

    tensix_reset(tt);

    /* Push 1 NOP to each core */
    uint32_t nop = 0x02000000;
    tensix_push(tt, nop, 0);
    tensix_push(tt, nop, 1);
    tensix_push(tt, nop, 2);

    /* Execute all cores in one step */
    tensix_step_all(tt);

    /* Check all cores executed 1 instruction */
    ASSERT_EQ(1, tt->cop->threads[0].insn_executed, "T0 executed");
    ASSERT_EQ(1, tt->cop->threads[1].insn_executed, "T1 executed");
    ASSERT_EQ(1, tt->cop->threads[2].insn_executed, "T2 executed");

    TEST_PASS();
}

/* Test 5: FIFO isolation */
void test_fifo_isolation(tensix_t *tt)
{
    TEST_START("FIFO isolation");

    tensix_reset(tt);

    /* Fill T0 FIFO */
    uint32_t nop = 0x02000000;
    for (int i = 0; i < 10; i++) {
        tensix_push(tt, nop, 0);
    }

    /* Check FIFO counts */
    ASSERT_EQ(10, tensix_cop_fifo_count(tt->cop, 0), "T0 FIFO count");
    ASSERT_EQ(0, tensix_cop_fifo_count(tt->cop, 1), "T1 FIFO empty");
    ASSERT_EQ(0, tensix_cop_fifo_count(tt->cop, 2), "T2 FIFO empty");

    /* Execute 5 from T0 */
    for (int i = 0; i < 5; i++) {
        tensix_step(tt, 0);
    }

    /* Check FIFO counts again */
    ASSERT_EQ(5, tensix_cop_fifo_count(tt->cop, 0), "T0 FIFO count after execution");
    ASSERT_EQ(0, tensix_cop_fifo_count(tt->cop, 1), "T1 FIFO still empty");
    ASSERT_EQ(0, tensix_cop_fifo_count(tt->cop, 2), "T2 FIFO still empty");

    TEST_PASS();
}

/* Test 6: Semaphore operations on different cores */
void test_semaphore_per_core(tensix_t *tt)
{
    TEST_START("Semaphore operations per core");

    tensix_reset(tt);

    /* Initialize semaphore 0 from T0 */
    uint32_t seminit0 = 0xA3A00004;  /* TTSEMINIT: max=10, init=0, sem_sel=(1<<0)<<2 */
    tensix_push(tt, seminit0, 0);
    tensix_step(tt, 0);

    printf("  T0 initialized sem[0] = %d (max=%d)\n", tt->sem[0], tt->sem_max[0]);

    /* Initialize semaphore 1 from T1
     * SEMINIT encoding (ckernel_ops.h):
     * - Bits [23:20] = max_value = 20
     * - Bits [19:16] = init_value = 0
     * - Bits [15:2]  = sem_sel bitmask = (1<<1)<<2
     */
    uint32_t seminit1 = 0xA3400008;  /* TTSEMINIT: max=20, init=0, sem_sel=(1<<1)<<2 */
    tensix_push(tt, seminit1, 1);
    tensix_step(tt, 1);

    printf("  T1 initialized sem[1] = %d (max=%d)\n", tt->sem[1], tt->sem_max[1]);

    /* Post to sem[0] from T0 */
    uint32_t sempost0 = 0xA4000004;  /* TTSEMPOST: sem_sel=(1<<0)<<2 */
    tensix_push(tt, sempost0, 0);
    tensix_step(tt, 0);

    /* Post to sem[1] from T2 */
    uint32_t sempost1 = 0xA4000008;  /* TTSEMPOST: sem_sel=(1<<1)<<2 */
    tensix_push(tt, sempost1, 2);
    tensix_step(tt, 2);

    /* Verify independent semaphore state */
    printf("  After posts: sem[0]=%d, sem[1]=%d\n", tt->sem[0], tt->sem[1]);
    ASSERT_EQ(1, tt->sem[0], "sem[0] incremented");
    ASSERT_EQ(1, tt->sem[1], "sem[1] incremented");

    TEST_PASS();
}

int main(void)
{
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  Tensix Coprocessor Test Suite (core_id interface)        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    /* Allocate memory regions (only T0/T1/T2 - B and NC don't use Tensix) */
    uint8_t *l1_mem = calloc(1, 0x180000);      // 1.5MB L1 scratchpad
    uint8_t *high_mem = calloc(1, 0x400000);    // 4MB high address region
    uint8_t *t0_ldm = calloc(1, 0x4000);        // 16KB per core
    uint8_t *t1_ldm = calloc(1, 0x4000);
    uint8_t *t2_ldm = calloc(1, 0x4000);

    if (!l1_mem || !high_mem || !t0_ldm || !t1_ldm || !t2_ldm) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 1;
    }

    /* Initialize Tensix coprocessor */
    tensix_t *tt = calloc(1, sizeof(tensix_t));
    if (!tt) {
        fprintf(stderr, "Failed to allocate Tensix instance\n");
        return 1;
    }

    tensix_init(tt, l1_mem, high_mem, t0_ldm, t1_ldm, t2_ldm);
    printf("\n✓ Tensix coprocessor initialized\n");

    /* Run all tests */
    test_single_core_execution(tt);
    test_independent_streams(tt);
    test_wait_isolation(tt);
    test_step_all(tt);
    test_fifo_isolation(tt);
    test_semaphore_per_core(tt);

    /* Cleanup */
    if (tt->cop) {
        free(tt->cop);
    }
    free(tt);
    free(l1_mem);
    free(high_mem);
    free(t0_ldm);
    free(t1_ldm);
    free(t2_ldm);

    /* Print summary */
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║  Test Summary                                              ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║  Passed: %2d                                                ║\n", tests_passed);
    printf("║  Failed: %2d                                                ║\n", tests_failed);
    printf("╚════════════════════════════════════════════════════════════╝\n");

    return (tests_failed > 0) ? 1 : 0;
}
