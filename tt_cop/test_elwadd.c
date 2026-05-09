/*
 * test_elwadd.c - Test program for ELWADD instruction
 * Tests the element-wise add operation with debugging output
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "tensix.h"
#include "tensix_cop.h"

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

/* Helper function to convert float to uint32_t for hex display */
uint32_t float_to_uint32(float f) {
    union { float f; uint32_t u; } converter;
    converter.f = f;
    return converter.u;
}

/* Test ELWADD instruction with simple values */
void test_elwadd_basic(tensix_t *tt)
{
    TEST_START("ELWADD Basic Operation");
    
    tensix_reset(tt);
    
    /* Set up test data in SrcA and SrcB registers */
    /* Use simple values that should add up correctly */
    float test_values_a[] = {1.0f, 2.0f, 3.0f, 4.0f};
    float test_values_b[] = {0.5f, 1.5f, 2.5f, 3.5f};
    
    /* Initialize SrcA row 0 */
    for (int j = 0; j < 4; j++) {
        tt->srca[0][j] = test_values_a[j];
    }
    tt->srca_rwc = 0;  /* Row 0, aligned to 8 */
    tt->srca_dvalid = true;
    
    /* Initialize SrcB row 0 */
    for (int j = 0; j < 4; j++) {
        tt->srcb[0][j] = test_values_b[j];
    }
    tt->srcb_rwc = 0;  /* Row 0 */
    tt->srcb_dvalid = true;
    
    /* Clear dest and set up for writing */
    memset(tt->dest, 0, sizeof(tt->dest));
    tt->dest_rwc = 0;
    tt->dest_offset = 0;
    tt->fidelity = 0;  /* HiFi4 mode - no scaling */
    
    printf("Before ELWADD:\n");
    printf("  SrcA[0][0..3] = %f %f %f %f\n", 
           tt->srca[0][0], tt->srca[0][1], tt->srca[0][2], tt->srca[0][3]);
    printf("  SrcB[0][0..3] = %f %f %f %f\n", 
           tt->srcb[0][0], tt->srcb[0][1], tt->srcb[0][2], tt->srcb[0][3]);
    printf("  Expected[0..3] = %f %f %f %f\n", 
           1.0f + 0.5f, 2.0f + 1.5f, 3.0f + 2.5f, 4.0f + 3.5f);
    
    /* Execute ELWADD instruction
     * Encoding: opcode<<24 | clear_dvalid<<22 | dest_accum_en<<21 | instr_mod19<<19 | addr_mode<<14 | dst<<0
     * opcode = 0x28 (ELWADD)
     * clear_dvalid = 0 (don't clear)
     * dest_accum_en = 0 (don't accumulate)
     * instr_mod19 = 0 (no broadcast)
     * addr_mode = 0 (no addr mod)
     * dst = 0 (destination row 0)
     */
    uint32_t elwadd_insn = (0x28 << 24) | (0 << 22) | (0 << 21) | (0 << 19) | (0 << 14) | 0;
    
    printf("Executing ELWADD instruction: 0x%08x\n", elwadd_insn);
    tensix_push(tt, elwadd_insn, 0);
    tensix_step(tt, 0);
    
    /* Check results */
    printf("After ELWADD:\n");
    printf("  Dest[0][0..3] = %f %f %f %f\n", 
           tt->dest[0][0], tt->dest[0][1], tt->dest[0][2], tt->dest[0][3]);
    printf("  Hex values: 0x%08x 0x%08x 0x%08x 0x%08x\n",
           float_to_uint32(tt->dest[0][0]), float_to_uint32(tt->dest[0][1]), 
           float_to_uint32(tt->dest[0][2]), float_to_uint32(tt->dest[0][3]));
    
    /* Verify calculations */
    bool pass = true;
    float expected[] = {1.5f, 3.5f, 5.5f, 7.5f};
    float tolerance = 1e-6f;
    
    for (int i = 0; i < 4; i++) {
        float diff = tt->dest[0][i] - expected[i];
        if (diff < 0) diff = -diff;
        if (diff > tolerance) {
            printf("  ERROR: Dest[0][%d] = %f, expected %f (diff = %f)\n", 
                   i, tt->dest[0][i], expected[i], diff);
            pass = false;
        }
    }
    
    if (pass) {
        TEST_PASS();
    } else {
        TEST_FAIL("Incorrect calculation results");
    }
}

/* Test ELWADD with fidelity scaling */
void test_elwadd_fidelity(tensix_t *tt)
{
    TEST_START("ELWADD with Fidelity Scaling");
    
    tensix_reset(tt);
    
    /* Set up simple test data */
    tt->srca[0][0] = 32.0f;  /* Will become 1.0 after /32 scaling */
    tt->srca[0][1] = 64.0f;  /* Will become 2.0 after /32 scaling */
    tt->srca_rwc = 0;
    tt->srca_dvalid = true;
    
    tt->srcb[0][0] = 32.0f;  /* Will become 1.0 after /32 scaling */
    tt->srcb[0][1] = 64.0f;  /* Will become 2.0 after /32 scaling */
    tt->srcb_rwc = 0;
    tt->srcb_dvalid = true;
    
    memset(tt->dest, 0, sizeof(tt->dest));
    tt->dest_rwc = 0;
    tt->dest_offset = 0;
    tt->fidelity = 1;  /* Enable /32 scaling */
    
    printf("Testing with fidelity=1 (32x scaling):\n");
    printf("  Input: SrcA[0][0]=32.0, SrcB[0][0]=32.0\n");
    printf("  Expected after /32 scaling: (32/32) + (32/32) = 1.0 + 1.0 = 2.0\n");
    
    uint32_t elwadd_insn = (0x28 << 24) | (0 << 22) | (0 << 21) | (0 << 19) | (0 << 14) | 0;
    tensix_push(tt, elwadd_insn, 0);
    tensix_step(tt, 0);
    
    printf("  Actual result: Dest[0][0] = %f\n", tt->dest[0][0]);
    printf("  Expected result: 2.0\n");
    
    float tolerance = 1e-6f;
    if ((tt->dest[0][0] - 2.0f < tolerance) && (tt->dest[0][0] - 2.0f > -tolerance)) {
        TEST_PASS();
    } else {
        TEST_FAIL("Fidelity scaling failed");
    }
}

int main()
{
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  ELWADD Instruction Test Suite                              ║\n");
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
        fprintf(stderr, "Failed to allocate Tensix structure\n");
        free(l1_mem);
        free(high_mem);
        free(t0_ldm);
        free(t1_ldm);
        free(t2_ldm);
        return 1;
    }
    
    tensix_init(tt, l1_mem, high_mem, t0_ldm, t1_ldm, t2_ldm);
    printf("✓ Tensix coprocessor initialized\n");
    
    /* Run tests */
    test_elwadd_basic(tt);
    test_elwadd_fidelity(tt);
    
    /* Cleanup */
    free(l1_mem);
    free(high_mem);
    free(t0_ldm);
    free(t1_ldm);
    free(t2_ldm);
    free(tt);
    
    /* Print summary */
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║  Test Summary                                              ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║  Passed:  %-16d                                    ║\n", tests_passed);
    printf("║  Failed:  %-16d                                    ║\n", tests_failed);
    printf("╚════════════════════════════════════════════════════════════╝\n");
    
    return tests_failed ? 1 : 0;
}