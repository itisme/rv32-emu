CC = gcc
LDFLAGS = -shared -Wl,--whole-archive ./rv32emu/src/softfloat/build/Linux-x86_64-GCC/softfloat.a -Wl,--no-whole-archive
CFLAGS = -O0 -g -Wall -Wextra -fPIC \
	     -DENABLE_ELF_LOADER=0 \
	     -DENABLE_EXT_C=0 \
	     -DENABLE_EXT_TT=1 \
         -Irv32emu \
         -Irv32emu/src \
         -Itt_cop \
         -Irv32emu/softfloat/source/include \
         -include rv32emu/src/common.h

RV32_SRCS = \
	rv32emu/src/riscv.c \
	rv32emu/src/decode.c \
	rv32emu/src/emulate.c \
	rv32emu/src/syscall.c \
	rv32emu/src/syscall_sdl.c \
	rv32emu/src/elf.c \
	rv32emu/src/utils.c \
	rv32emu/src/io.c \
	rv32emu/src/log.c \
	rv32emu/src/mpool.c \
	rv32emu/src/map.c

# Tensix coprocessor sources
TT_COP_SRCS = \
	tt_cop/tensix_impl.c \
	tt_cop/tensix_cop.c \
	tt_cop/tt_insn.c

SRCS = rv32sim.c $(RV32_SRCS) $(TT_COP_SRCS)
OBJS = $(SRCS:.c=.o)

TARGET = librv32sim.so

all: check_rv32emu $(TARGET)

check_rv32emu:
	@if [ ! -f rv32emu/src/riscv.h ]; then \
		echo "Error: rv32emu/src/riscv.h not found."; \
		echo "Please run: git clone https://github.com/sysprog21/rv32emu.git"; \
		exit 1; \
	fi

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean check_rv32emu
