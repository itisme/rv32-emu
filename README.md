# tenstorrent-rv32-emu

[中文说明](README_zh.md)

RV32IMC instruction set emulator with Tenstorrent Tensix coprocessor support. Built as a shared library (`librv32sim.so`) for use with the [tenstorrent-qemu-update](https://github.com/xxx/tenstorrent-qemu-update) QEMU device.

## Features

### RV32IMC Emulator
- Based on [rv32emu](https://github.com/sysprog21/rv32emu)
- RV32I base + M (multiply/divide) + C (compressed) extensions
- Custom TTINSN extension for Tensix coprocessor interaction
- Shared library interface: create, run, halt, destroy CPU instances

### Tensix Coprocessor
- **3-thread execution**: T0 (unpackr), T1 (math), T2 (packr)
- **Wait Gate**: STALLWAIT / SEMWAIT / STREAMWAIT synchronization
- **MOP Expander**: Template 0 (mask iteration), Template 1 (nested loop)
- **~200 instruction opcodes**: arithmetic, data movement, synchronization
- **8 semaphores + 8 mutexes** per coprocessor
- **dvalid synchronization**: inter-thread data dependency tracking
- **Instruction FIFO**: 32-entry queue per thread

## Build

```bash
# Clone with dependencies
git clone https://github.com/xxx/tenstorrent-rv32-emu.git rv32_emu
cd rv32_emu

# Clone the rv32emu base emulator
git clone https://github.com/sysprog21/rv32emu.git

# Build softfloat (required dependency)
cd rv32emu/src/softfloat/build/Linux-x86_64-GCC
make -j$(nproc)
cd ../../../../..

# Build the shared library
make -j$(nproc)
```

This produces `librv32sim.so`.

## Directory Structure

```
rv32_emu/
├── rv32sim.c/h          # Shared library interface (create/run/halt CPU)
├── Makefile             # Build configuration
├── rv32emu/             # Base RV32IMC emulator (git clone)
│   └── src/
│       ├── riscv.c/h    # Core emulator
│       ├── decode.c     # Instruction decoder
│       ├── emulate.c    # Instruction execution
│       └── softfloat/   # Soft float library
└── tt_cop/              # Tensix coprocessor
    ├── tensix_cop.c/h   # COP core: Wait Gate, MOP, step logic
    ├── tensix_impl.c    # Instruction implementations
    ├── tensix.h         # Tensix constants and definitions
    ├── tt_insn.c        # TTINSN custom instruction handler
    ├── test_ttcop.c     # COP unit tests
    └── test_wait_behavior.c  # Wait Gate behavior tests
```

## API

```c
// Create a CPU instance with memory and callbacks
void* rv32sim_create(void* mem, uint32_t mem_size, ...);

// Run until halt or coroutine yield
void rv32sim_run(void* cpu);

// Run in coroutine mode (cooperative multitasking)
void rv32sim_run_coroutine(void* cpu);

// Halt a running CPU
void rv32sim_halt(void* cpu);

// Destroy CPU instance
void rv32sim_destroy(void* cpu);
```

## Tensix Node Architecture

Each Tensix node in the Blackhole chip contains 7 RV32IMC cores:

| Core | Role | Description |
|------|------|-------------|
| B_CORE | Boot/Brisc | Bootstrap and control |
| N_CORE | Network | NOC data movement |
| T0 | Unpackr thread | Data unpacking, feeds T1 |
| T1 | Math thread | Compute operations |
| T2 | Packr thread | Data packing, consumes T1 output |
| DMA_R | DMA Read | Memory read engine |
| DMA_W | DMA Write | Memory write engine |

T0/T1/T2 share a Tensix coprocessor with dvalid-based synchronization.

## License

This project is for research and educational purposes.
