// rv32sim.c
#define RV32_FEATURE_EXT_M 1
#define RV32_FEATURE_EXT_A 1
#define RV32_FEATURE_EXT_C 1
#define RV32_FEATURE_Zicsr 1
#define RV32_FEATURE_Zifencei 1
#define RV32_FEATURE_SYSTEM 0

#include <stdlib.h>
#include <string.h>

#include "rv32emu/src/common.h"
#include "rv32emu/src/riscv.h"
#include "rv32emu/src/riscv_private.h"
#include "rv32sim.h"

#define CYCLE_PER_STEP 10

#define MEM_L1_SIZE (1536 * 1024)

rv32_cpu_t rv32_create(uint8_t *base, int size, uint8_t *mem_local, int local_size,
                       uint32_t start, int cycles, tensix_t *tensix, int core_id) {

    vm_attr_t * attr = calloc(1, sizeof(vm_attr_t));
    attr->mem = calloc(1, sizeof(memory_t));
    attr->mem->mem_base = base;
    attr->mem->mem_size = size;
    attr->mem->mem_high = base + size;
    attr->mem->mem_local = mem_local;
    attr->mem->local_size = local_size;
    attr->mem_size = size; // 2M memory for example
    attr->cycle_per_step = cycles;
    attr->allow_misalign = false;
    attr->data.user.elf_program = "dummy"; // No ELF loader in this example
    //attr->run_flag = 1 << 1; // Enable GDBSTUB for example

    return rv_new(attr, start, tensix, core_id);
}

void rv32_run(rv32_cpu_t handle) {
    rv_run(handle);
}

void rv32_run_co(rv32_cpu_t handle, int max_instructions, void (*yield_cb)(void *, void *), void *cb_arg0, void *cb_arg1) {
    riscv_t *rv = (riscv_t *)handle;
    if (rv_has_halted(rv)) {
        return; // CPU already halted
    }
    
    int instructions_executed = 0;

    while (!rv_has_halted(rv)) {
        rv_step(rv);
        instructions_executed++;
        if(instructions_executed >= max_instructions) {
            (*yield_cb)(cb_arg0, cb_arg1);
            instructions_executed = 0;
        }
    }

}

void rv32_halt(rv32_cpu_t handle) {
    rv_halt(handle);
}

int rv32_has_halted(rv32_cpu_t handle) {
    return rv_has_halted(handle);
}

uint32_t rv32_get_pc(rv32_cpu_t handle) {
    return rv_get_pc((riscv_t *)handle);
}

void rv32_destroy(rv32_cpu_t cpu) {
    riscv_t *rv = (riscv_t *)cpu;
    vm_attr_t *attr = PRIV(rv);
    free(attr->mem);
    free(attr);
    rv_delete(cpu);
}