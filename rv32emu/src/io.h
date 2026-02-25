/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdint.h>
#include <string.h>

#define HIGH_MEM_OFFSET 0xffb00000
#define LOCAL_MEM_SIZE 0x2000

/* main memory */
typedef struct {
    uint8_t *mem_base;
    uint64_t mem_size;
    uint8_t *mem_high;
    uint8_t *mem_local;
    uint32_t local_size;
} memory_t;

/* create a memory instance */
memory_t *memory_new(uint32_t size);

/* delete a memory instance */
void memory_delete(memory_t *m);

/* read an instruction from memory */
uint32_t memory_ifetch(memory_t *m, uint32_t addr);

/* read a word from memory */
uint32_t memory_read_w(memory_t *m, uint32_t addr);

/* read a short from memory */
uint16_t memory_read_s(memory_t *m, uint32_t addr);

/* read a byte from memory */
uint8_t memory_read_b(memory_t *m, uint32_t addr);

/* read a length of data from memory */
void memory_read(const memory_t *m, uint8_t *dst, uint32_t addr, uint32_t size);

/* write a length of data to memory */
static inline bool memory_write(memory_t *m,
                                uint32_t addr,
                                const uint8_t *src,
                                uint32_t size)
{
    /* Bounds checking to prevent buffer overflow */
    //if (addr >= m->mem_size || size > m->mem_size - addr)
    //    return false;
    uint8_t *dst = addr < HIGH_MEM_OFFSET ? m->mem_base + addr : m->mem_high + (addr - HIGH_MEM_OFFSET);
    memcpy(dst, src, size);
    return true;
}

/* write a word to memory */
void memory_write_w(memory_t *m, uint32_t addr, const uint8_t *src);

/* write a short to memory */
void memory_write_s(memory_t *m, uint32_t addr, const uint8_t *src);

/* write a byte to memory */
void memory_write_b(memory_t *m, uint32_t addr, const uint8_t *src);

/* write a length of certain value to memory */
static inline bool memory_fill(memory_t *m,
                               uint32_t addr,
                               uint32_t size,
                               uint8_t val)
{
    /* Bounds checking to prevent buffer overflow */
    if (addr >= m->mem_size || size > m->mem_size - addr)
        return false;
    memset(m->mem_base + addr, val, size);
    return true;
}
