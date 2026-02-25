# Makefile for Tensix Coprocessor Library (tt_cop)
# Standalone build and test for tt_cop module

CC = gcc
CFLAGS = -O2 -g -Wall -Wextra -std=c11 -I../rv32emu/src
TEST_CFLAGS = -O0 -g -Wall -Wextra -std=c11 -I../rv32emu/src

# Source files for tt_cop library
SRCS = tensix_impl.c \
       tensix_cop.c \
       tt_insn.c

# Object files
OBJS = $(SRCS:.c=.o)

# Test programs
TEST_PROGS = test_ttcop test_wait_behavior

# Default target - build and run tests
all: build test

# Build all object files
build: $(OBJS)
	@echo "✓ tt_cop library objects built successfully"

# Compile object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Dependencies (io.h from rv32emu/src via -I flag)
tensix_impl.o: tensix_impl.c tensix.h tensix_cop.h
tensix_cop.o: tensix_cop.c tensix_cop.h tensix.h
tt_insn.o: tt_insn.c tensix.h

# Build test programs
test_ttcop: test_ttcop.c $(OBJS)
	$(CC) $(TEST_CFLAGS) -o $@ $^
	@echo "✓ test_ttcop built successfully"

test_wait_behavior: test_wait_behavior.c $(OBJS)
	$(CC) $(TEST_CFLAGS) -o $@ $^
	@echo "✓ test_wait_behavior built successfully"

# Run tests
test: $(TEST_PROGS)
	@echo ""
	@echo "========================================="
	@echo "Running Tensix Coprocessor Unit Tests"
	@echo "========================================="
	@echo ""
	@./test_ttcop
	@echo ""
	@echo "-----------------------------------------"
	@echo ""
	@./test_wait_behavior
	@echo ""
	@echo "========================================="
	@echo "All Tests Complete"
	@echo "========================================="

# Run individual tests
run-basic: test_ttcop
	./test_ttcop

run-wait: test_wait_behavior
	./test_wait_behavior

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TEST_PROGS)
	@echo "✓ Cleaned build artifacts"

# Clean and rebuild
rebuild: clean all

# Help
help:
	@echo "Tensix Coprocessor (tt_cop) Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all           - Build and run all tests (default)"
	@echo "  build         - Build library object files only"
	@echo "  test          - Build and run all test programs"
	@echo "  run-basic     - Run basic functionality tests"
	@echo "  run-wait      - Run wait instruction tests"
	@echo "  clean         - Remove all build artifacts"
	@echo "  rebuild       - Clean and rebuild everything"
	@echo "  help          - Show this help message"

.PHONY: all build test run-basic run-wait clean rebuild help
