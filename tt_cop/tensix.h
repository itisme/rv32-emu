#ifndef __TENSIX_H__
#define __TENSIX_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* Log levels */
enum { TT_LOG_ERROR = 0, TT_LOG_WARN = 1, TT_LOG_INFO = 2, TT_LOG_DEBUG = 3 };
extern int tt_log_level;
#define TT_DBG(fmt, ...) do { if (tt_log_level >= TT_LOG_DEBUG) printf(fmt, ##__VA_ARGS__); } while(0)
#define TT_INFO(fmt, ...) do { if (tt_log_level >= TT_LOG_INFO) printf(fmt, ##__VA_ARGS__); } while(0)
#define TT_WARN(fmt, ...) do { if (tt_log_level >= TT_LOG_WARN) fprintf(stderr, fmt, ##__VA_ARGS__); } while(0)
#define TT_ERR(fmt, ...) do { if (tt_log_level >= TT_LOG_ERROR) fprintf(stderr, fmt, ##__VA_ARGS__); } while(0)

/* Memory interface:
 * When integrated with rv32_emu, memory_t is included via common.h -> io.h
 * When compiled standalone, we don't need memory_t (only use tensix_memory_t)
 */

/* tensix address mode */
#define SRCA_ROWS 64
#define SRCB_ROWS 64
#define DEST_ROWS 1024
#define ROW_SIZE 16

#define CLR_A     0x1
#define CLR_B     0x2
#define CLR_AB    0x3
#define CLR_NONE  0x0

#define SET_A     0x1
#define SET_B     0x2
#define SET_AB    0x3
#define SET_D     0x4
#define SET_AD    0x5
#define SET_BD    0x6
#define SET_ABD   0x7
#define SET_F     0x8
#define SET_A_F   0x9
#define SET_B_F   0xa
#define SET_AB_F  0xb
#define SET_D_F   0xc
#define SET_AD_F  0xd
#define SET_BD_F  0xe
#define SET_ABD_F 0xf

#define CR_A         0x1
#define CR_B         0x2
#define CR_AB        0x3
#define CR_D         0x4
#define CR_AD        0x5
#define CR_BD        0x6
#define CR_ABD       0x7
#define C_TO_CR_MODE 0x8

#define STALL_TDMA    0x1
#define STALL_SYNC    0x2
#define STALL_PACK    0x4
#define STALL_UNPACK  0x8
#define STALL_XMOV    0x10
#define STALL_THCON   0x20
#define STALL_MATH    0x40
#define STALL_CFG     0x80
#define STALL_SFPU    0x100
#define STALL_THREAD  0x1ff

#define STALL_ON_ZERO 0x1
#define STALL_ON_MAX  0x2

/* SFPU Local Registers: 17 registers × 32 lanes, each 32-bit */
/* LReg[0-7]: general purpose (read/write)
 * LReg[8]:  read-only, all lanes = 0.8373 (FP32)
 * LReg[9]:  read-only, all lanes = 0
 * LReg[10]: read-only, all lanes = 1.0 (FP32)
 * LReg[11-14]: writable only via SFPCONFIG
 * LReg[15]: read-only, lane i = 2*i (0,2,4,...,62)
 * LReg[16]: writable only via SFPLOADMACRO
 */
#define LREG_COUNT    17
#define LREG_LANES    32

#define DMA_REG_COUNT 64

#define MUTEX_NONE 0xff

#define THD_REG_COUNT 57
/* Blackhole: CFG_STATE_SIZE=56, Config[2][56*4], so 224 regs per state */
#define CFG_REG_COUNT (56 * 4)

/* TENSIX_CFG_BASE offset in high_mem
 * high_mem starts at 0xFFB00000 (with local_size offset handled by rv32emu)
 * So offset = 0xFFEF0000 - 0xFFB00000 = 0x3F0000
 */
#define TENSIX_CFG_OFFSET_IN_HIGH_MEM 0x3F0000


#define DEST_FACE_WIDTH 16
#define DEST_FACE_WIDTH_LOG2 4
#define DEST_FACE_HEIGHT 16
#define DEST_FACE_HEIGHT_LOG2 4
#define DEST_REGISTER_FULL_SIZE (64 * DEST_FACE_HEIGHT)
#define DEST_REGISTER_FULL_SIZE_LOG2 12
#define DEST_REGISTER_HALF_SIZE (DEST_REGISTER_FULL_SIZE / 2)
#define BIT32_DEST_REGISTER_HALF_SIZE (DEST_REGISTER_HALF_SIZE / 2)

#define DEST_REGISTER_FULL_SIZE_BYTES (DEST_REGISTER_FULL_SIZE * 2 * 16)
#define DEST_REGISTER_HALF_SIZE_BYTES (DEST_REGISTER_FULL_SIZE_BYTES / 2)

// MOP config registers
#define TENSIX_MOP_CFG_BASE 0xFFB80000  // 0xFFB8000 - 0xFFB8100

// CLR, CR, INCR (4 bits)
typedef struct Src {
    uint32_t incr;
    uint32_t clr;
    uint32_t cr;
} Src;
// CLR, CR, INCR (8 bits)
typedef struct Dest {
    uint32_t incr;
    uint32_t clr;
    uint32_t cr;
    uint32_t c_to_cr;
} Dest;
// CLR, INCR (2 bits)
typedef struct Fidelity {
    uint32_t incr;
    uint32_t clr;
} Fidelity;
// CLR, INCR (4 bits)
typedef struct Bias {
    uint32_t incr;
    uint32_t clr;
} Bias;

typedef struct AddrMod {
    Src srca;
    Src srcb;
    Dest dest;
    Fidelity fidelity;
    Bias bias;

} AddrMod;

// CLR, CR, INCR (4 bits)
typedef struct AddrModVals {
    uint32_t incr;
    uint32_t clr;
    uint32_t cr;
} AddrModVals;

typedef struct AddrModReduced {
    uint32_t incr;
    uint32_t clr;
} AddrModReduced;

typedef struct AddrModPack {
    AddrModVals y_src;
    AddrModVals y_dst;
    AddrModReduced z_src;
    AddrModReduced z_dst;
} AddrModPack;

typedef enum DataFormat {
    Float32   = 0,
    Float16   = 1,
    Bfp8      = 2,
    Bfp4      = 3,
    Bfp2      = 11,
    Float16_b = 5,
    Bfp8_b    = 6,
    Bfp4_b    = 7,
    Bfp2_b    = 15,
    Lf8       = 10,
    UInt16    = 12,
    Int8      = 14,
    UInt8     = 30,
    Int32     = 8,
    Int16     = 9,
    Tf32      = 4,
    testMan7  = 0x82,       // intermediate format for testing: 7bit mantissa (6+hidden)
    testMan2  = 0x8A,       // intermediate format for testing: 2bit mantissa (2+hidden)
    Invalid   = 0xff
} DataFormat;

typedef struct CfgCntx {
    uint32_t base_addr;
    uint32_t offset_addr;
} CfgCntx;

typedef struct AddrCtrl {
    // controlled by CFG registers
    DataFormat tile_in_data_format;
    uint32_t tile_x_dim;
    uint32_t tile_y_dim;
    uint32_t tile_z_dim;
    uint32_t tile_w_dim;
    uint32_t ch0_x_stride;
    uint32_t ch0_y_stride;
    uint32_t ch0_z_stride;
    uint32_t ch0_w_stride;
    // no ch1_x_stride
    uint32_t ch1_y_stride;
    uint32_t ch1_z_stride;
    uint32_t ch1_w_stride;
    CfgCntx cntx[8];
    uint32_t unp_cntx_offset;
    // controlled by SETADC* instructions
    uint32_t ch0_x;
    uint32_t ch0_y;
    uint32_t ch0_z;
    uint32_t ch0_w;
    uint32_t ch1_x;
    uint32_t ch1_y;
    uint32_t ch1_z;
    uint32_t ch1_w;
    uint32_t x_end;
    uint32_t ch0_x_cr;
    uint32_t ch0_y_cr;
    uint32_t ch0_z_cr;
    uint32_t ch0_w_cr;
    uint32_t ch1_x_cr;
    uint32_t ch1_y_cr;
    uint32_t ch1_z_cr;
    uint32_t ch1_w_cr;
    // state flags
    bool update_strides;
} AddrCtrl;

typedef struct PackConfig PackConfig;
struct PackConfig {
    bool pack_fp32_dest;
    DataFormat pack_src_format; 
    DataFormat pack_dst_format;
    uint32_t pack_x_dim; 
    uint32_t pack_y_dim; 
    uint32_t pack_z_dim;
    uint32_t pack_ch0_y_stride;
    uint32_t pack_ch0_z_stride;
    uint32_t pack_ch0_w_stride;
    uint32_t pack_ch1_x_stride;
    uint32_t pack_ch1_y_stride;
    uint32_t pack_ch1_z_stride;
    uint32_t pack_ch1_w_stride;
    uint32_t pack_l1_offset;
    uint32_t relu_type;
    uint32_t relu_threshold;
    uint32_t pack_dest_offset[4];
    uint32_t pack_dest_addr;
    bool pack_l1_acc;
    bool pack_update_strides;
};

typedef void (*TemplateOp)();

typedef struct {
    bool unpack_b;
    bool unpack_halo;
    TemplateOp a0_instr;
    TemplateOp a1_instr;
    TemplateOp a2_instr; 
    TemplateOp a3_instr;
    TemplateOp b_instr;
    TemplateOp skip_a_instr;
    TemplateOp skip_b_instr; 
} MopTemplate0;

typedef struct {
    uint32_t loop_outer;
    TemplateOp start_op;
    uint32_t loop_inner;
    TemplateOp loop_op0;
    TemplateOp loop_op1;
    TemplateOp last_inner_op;
    TemplateOp last_outer_op;
    TemplateOp end_op0;
    TemplateOp end_op1;
} MopTemplate1;

/* Forward declarations */
typedef struct tensix tensix_t;
typedef struct tensix_cop tensix_cop_t;

/* Memory map structure to hold all memory pointers
 * Note: Only includes memory regions that Tensix coprocessor needs to access.
 * B and NC cores don't use Tensix, so their local memory is not included here.
 *
 * Memory layout:
 * - l1_scratchpad: 1.5MB shared memory (0x00000000 - 0x0017FFFF)
 * - high_mem: 4MB high address region (0xFFB00000 - 0xFFEFFFFF)
 * - t0/t1/t2_ldm: 16KB per-core memory, each containing:
 *   * Offset 0x0000-0x0FFF (4KB): Fast LDM (maps to 0xFFB00000)
 *   * Offset 0x1000-0x2FFF (8KB): Slow LDM (maps to 0xFFB18000/1A000/1C000)
 *   * Offset 0x3000-0x3FFF (4KB): REGFILE/GPRs (maps to 0xFFE00000)
 */
typedef struct {
    uint8_t *l1_scratchpad;  // 0x00000000 - 0x0017FFFF (1.5MB)
    uint8_t *high_mem;       // 0xFFB00000 - 0xFFEFFFFF (4MB)
    uint8_t *t0_ldm;         // TRISC0 local memory (16KB)
    uint8_t *t1_ldm;         // TRISC1 local memory (16KB)
    uint8_t *t2_ldm;         // TRISC2 local memory (16KB)
} tensix_memory_t;

/* Read config register from high_mem (where TRISC writes via MMIO) */
static inline uint32_t tensix_read_cfg(tensix_memory_t *mem, uint32_t reg_idx) {
    if (mem->high_mem && reg_idx < CFG_REG_COUNT) {
        return *(uint32_t *)(mem->high_mem + TENSIX_CFG_OFFSET_IN_HIGH_MEM + reg_idx * 4);
    }
    return 0;
}

/* Write config register to high_mem (for WRCFG instruction) */
static inline void tensix_write_cfg(tensix_memory_t *mem, uint32_t reg_idx, uint32_t value) {
    if (mem->high_mem && reg_idx < CFG_REG_COUNT) {
        *(uint32_t *)(mem->high_mem + TENSIX_CFG_OFFSET_IN_HIGH_MEM + reg_idx * 4) = value;
    }
}

struct tensix {
    tensix_memory_t mem;     /* Memory regions accessible by Tensix */
    tensix_cop_t *cop;       /* Coprocessor state machine */

    float srca[SRCA_ROWS][ROW_SIZE];
    float srcb[SRCB_ROWS][ROW_SIZE];
    float dest[DEST_ROWS][ROW_SIZE];
    bool srca_dvalid;
    bool srcb_dvalid;
    bool dest_dvalid;  /* Dest register contains valid data (set by ELWADD, cleared by PACR) */
    uint32_t pack_l1_write_offset;  /* Running L1 write byte offset for packer (auto-increments) */
    uint32_t pack_l1_dest_addr_raw; /* Last raw cfg[69] value, to detect new tile */
    /* Per-thread RWC counters (ISA: RWCs[CurrentThread]) */
    uint32_t srca_rwc[3];
    uint32_t srca_rwc_cr[3];
    uint32_t srcb_rwc[3];
    uint32_t srcb_rwc_cr[3];
    uint32_t dest_rwc[3];
    uint32_t dest_rwc_cr[3];
    uint32_t fidelity[3];
    uint32_t bias;
    /* address mode */
    AddrMod addr_mod[8];
    AddrModPack addr_mod_pack[8];
    uint32_t dest_offset;
    uint32_t haloize_mode;    // applies to SrcA only
    uint32_t tileize_mode;
    uint32_t shift_amount;

    uint32_t tile_x_dim;
    uint32_t ovrd_ch0_x_stride;
    uint32_t ovrd_ch0_y_stride;
    uint32_t ovrd_ch0_z_stride;
    uint32_t ovrd_ch0_w_stride;
    uint32_t ovrd_ch1_y_stride;
    uint32_t ovrd_ch1_z_stride;
    uint32_t ovrd_ch1_w_stride;

    AddrCtrl adc[3];

    uint32_t mutex[8];
    uint32_t sem[8];
    uint32_t sem_max[8];

    uint32_t thread_id;          /* Deprecated: use tid parameter instead */

    uint32_t thd_reg[3][THD_REG_COUNT];    // 16-bit
    uint32_t cfg_reg[2][CFG_REG_COUNT];
    uint32_t cfg_state_id[3];

    // dma
    uint32_t dma_reg[3][DMA_REG_COUNT];

    // according to tt-metal/tt_metal/third_party/tt_llk/tt_llk_blackhole/common/inc/ckernel_instr_params.h
    PackConfig pack_configs[4];

    uint32_t pack_edge_offset_mask[4]; 
    uint32_t pack_edge_row_set_select[4];
    uint32_t tile_row_set_mapping[4];

    /* SFPU Local Registers */
    uint32_t lreg[LREG_COUNT][LREG_LANES];

    /* SFPU Lane Predication (per-lane conditional execution) */
    bool lane_flags[LREG_LANES];               /* Per-lane condition flags */
    bool use_lane_flags[LREG_LANES];           /* Per-lane: use LaneFlags for LaneEnabled? */
#define FLAG_STACK_DEPTH 8
    struct {
        bool lane_flags[LREG_LANES];
        bool use_lane_flags[LREG_LANES];
    } flag_stack[FLAG_STACK_DEPTH];
    int flag_stack_top;                        /* -1 = empty */

    uint32_t sem_math_pack;

    float neginf;

    // for thread ops
    uint32_t mop_type;
    MopTemplate0 mop_templ_0;
    MopTemplate1 mop_templ_1;
    uint32_t log2_replay_buffer_depth;
    TemplateOp replay_buffer[16];
    uint32_t replay_index;
    uint32_t replay_left;
    bool replay_execute_while_loading;

};

/* memory_t is defined in rv32emu/src/io.h */

/* Initialize tensix coprocessor with memory regions
 * Parameters:
 *   tt               - Tensix coprocessor instance
 *   l1_scratchpad_mem - L1 scratchpad RAM (1.5MB, 0x00000000 - 0x0017FFFF)
 *   high_mem         - High address region (4MB, 0xFFB00000 - 0xFFEFFFFF)
 *   t0_ldm           - TRISC0 local memory (16KB, includes fast LDM + slow LDM + REGFILE)
 *   t1_ldm           - TRISC1 local memory (16KB, includes fast LDM + slow LDM + REGFILE)
 *   t2_ldm           - TRISC2 local memory (16KB, includes fast LDM + slow LDM + REGFILE)
 *
 * Note: B and NC local memory not included (they don't use Tensix)
 *
 * Per-core LDM layout (16KB each):
 *   - Offset 0x0000-0x0FFF (4KB): Fast access LDM, maps to virtual 0xFFB00000
 *   - Offset 0x1000-0x2FFF (8KB): Slow access LDM, maps to virtual 0xFFB1x000
 *   - Offset 0x3000-0x3FFF (4KB): REGFILE/GPRs, maps to virtual 0xFFE00000
 */
void tensix_init(tensix_t *tt,
                 uint8_t *l1_scratchpad_mem,
                 uint8_t *high_mem,
                 uint8_t *t0_ldm,
                 uint8_t *t1_ldm,
                 uint8_t *t2_ldm);

/* Push instruction to tensix coprocessor FIFO (for sw to INSTRN_BUF) */
bool tensix_push(tensix_t *tt, uint32_t insn, int core_id);  //core_id coded with t0 (0) ~ t2 (2)

/* Run one cycle of tensix coprocessor (process FIFO instructions) */
bool tensix_step(tensix_t *tt, int core_id);

/* Direct instruction interface (for inline TT instructions in code)
 * set_direct: Set a pending direct instruction for execution
 * step_direct: Execute the direct instruction, with blocking support
 * Returns: true = execution completed, false = blocked by Wait Gate
 */
void tensix_set_direct(tensix_t *tt, uint32_t insn, int core_id);
bool tensix_step_direct(tensix_t *tt, int core_id);

void tensix_step_all(tensix_t *tt);

/* Reset tensix coprocessor state */
void tensix_reset(tensix_t *tt);

/* Execute a tensix instruction.
 * Returns true if completed, false if blocked/incomplete.
 */
bool tensix_execute_insn(tensix_t *tt, uint32_t insn, int tid);

/* Write to MOP_CFG registers (0xFFB80000), called from rv32emu MMIO handler */
void tensix_write_mop_cfg(tensix_t *tt, int core_id, uint32_t offset, uint32_t data);

/* Generic MMIO write handler for all TT-specific addresses.
 * Called from rv32emu on_mem_write_w for addresses that may need
 * special hardware behavior (MOP_CFG, instruction push, stream overlay, etc.)
 * Returns true if the write was fully handled (no further memory_write needed),
 * false if normal memory write should still proceed.
 */
/* MMIO write handler (returns true if handled) */
bool tensix_mmio_write(tensix_t *tt, int core_id, uint32_t addr, uint32_t data);

/* MMIO read handler (returns true if handled, value written to *result) */
bool tensix_mmio_read(tensix_t *tt, int core_id, uint32_t addr, uint32_t *result);

#endif