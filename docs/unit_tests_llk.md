# unit_tests_llk 调试记录

测试二进制：`tt_srcs/new/tt-metal/build_Debug/test/tt_metal/unit_tests_llk`

运行单个测试文件对应的用例：
```bash
./unit_tests_llk --gtest_filter="MeshDeviceFixture.TensixComputePackUntilize*"
```

---

## 修复记录

### 2026-04-30: JIT block cache 污染导致 dispatch hang

**文件**: `rv32emu/src/emulate.c`

**测试**: `TensixComputePackUntilizeDstTinyTile`（test_untilize_tilize.cpp）

**症状**: 连续多次 dispatch 时，后续 dispatch 卡死（hang）。具体表现：
- d1/d2 使用 `fp32_true` kernel（load offset = 0x41d0）
- d3 使用 `fp32_false` kernel（load offset = 0x41e0，偏移 0x10）
- d3 的 NCRISC 进入 kernel 后从不写 `tiles_received`，导致 T0 永久等待

**根因**: JIT block cache 污染。d1/d2 编译的 function epilogue 缓存在 PC=0xa440，d3 的一条 `beqz` 指令运行时 PC 恰好也是 0xa440（因 load offset 差 0x10），命中了 d1/d2 的缓存块，执行了错误的 epilogue 直接 ret，跳过了 NCRISC 的 tile 推送逻辑。

**原始错误做法**: `block_map_clear` 在 kernel 启动时（firmware→kernel 跳转）调用，每次都清，但对 NCRISC 无效（rv->tensix 为 NULL，走不同分支）。

**修复**: 将 TRISC 的 `block_map_clear` 移至 **kernel 执行完毕返回 firmware** 时调用（kernel→firmware 跳转）；NCRISC 单独处理，在 firmware→kernel 时仅当 kernel 入口 PC 发生变化才清缓存：

```c
/* TRISC cores (rv->tensix != NULL) */
} else if (rv->pre_pc >= TENSIX_KERNEL_ADDR_THRESHOLD &&
           rv->PC    <  TENSIX_KERNEL_ADDR_THRESHOLD) {
    /* kernel → firmware: 此时清缓存，防止污染下次 dispatch */
    block_map_clear(rv);
    rv->prev = NULL;
    ...
}

/* NCRISC (core_id == 3, rv->tensix == NULL) */
if (rv->pre_pc < TENSIX_KERNEL_ADDR_THRESHOLD && rv->PC >= TENSIX_KERNEL_ADDR_THRESHOLD) {
    static uint32_t ncrisc_prev_kernel_pc = 0;
    if (ncrisc_prev_kernel_pc != rv->PC) {  /* 入口 PC 变了才清 */
        block_map_clear(rv);
        rv->prev = NULL;
        ncrisc_prev_kernel_pc = rv->PC;
    }
}
```

**关键点**: 在 kernel 返回后清缓存而非进入前，允许同一 kernel 在多个 dispatch 间复用 JIT 缓存，同时防止不同 kernel 因地址重叠造成污染。

---

### 2026-05-12: PACR DST_ACCESS_STRIDED_MODE 下 packer 读错 DEST 偏移

**文件**: `tt_cop/tt_insn.c` — `ttpacr()`

**测试**: `TensixComputePackUntilizeDstTinyTile`（test_untilize_tilize.cpp）  
具体失败用例：`num_tiles_r=2, num_tiles_c=1, FP32_DestAcc=true, DstSyncFull=false`

**症状**: DstSyncFull=false 时第二个 tile 输出数据错误；DstSyncFull=true 通过。

**根因**: `pack_untilize` 使用 `TWO_INTFS_ACTIVE`（packer 0 + packer 1 同时工作）。
当 `dst_access_mode=1`（DST_ACCESS_STRIDED_MODE）时，packer i 读取同一 tile 的第 i 个 face，
所有 packer 共享相同的 tile base offset（存于 `cfg[180]`）。

原代码每个 packer 读自己的 `cfg[180+pck]`：
```c
// 错误：pck=1 读 cfg[181]，firmware 从不设置 cfg[181]，永远为 0
uint32_t dest_offset_p = tensix_read_cfg(&tt->mem, 180 + pck) & 0xFFF;
```

DstSyncFull=false 时，tile 1 的 firmware 设置 `cfg[180]=0x200`（=512，对应 DEST 行 512），
但 `cfg[181..183]` 始终为 0。pck=1 得到 offset=0，读取 DEST 行 16（错误），
而非行 528（正确）。

**修复**: strided mode 下所有 packer 统一读 `cfg[180]`：

```c
/* In DST_ACCESS_STRIDED_MODE all packers read different faces of the same tile;
 * firmware only sets cfg[180] (pck=0), so all packers must share that base offset. */
uint32_t dest_off_idx = dst_access_mode ? 180u : (180u + (uint32_t)pck);
uint32_t dest_offset_p = (dest_off_idx < CFG_REG_COUNT) ?
    (tensix_read_cfg(&tt->mem, dest_off_idx) & 0xFFF) : 0;
```

**原理**: strided mode 的设计意图是"所有 packer 分工读同一 tile 的各个 face"，
tile 在 DEST 中的位置由 `cfg[180]` 统一描述；packer i 在此基础上自动加 `i×256` datums
（`addr_p += pck * 256`）定位到对应 face，无需各自独立的 offset。

---

### 2026-05-24: UNPACR 缺少 Haloize_mode（within-face 16×16 转置）

**文件**: `tt_cop/tt_insn.c` — `ttunpacr()`

**测试**: `TensixComputeTransposeWH`、`TensixComputeTransposeWHShortInit`、`TensixComputeTransposeWHDest`（test_transpose.cpp）

**症状**: BF16 WH transpose 输出值错误（如 a2=23.625 vs b2=26.875）；Int32 路径（TransposeWHDest）使用 MOVD2B/TRNSPSRCB/MOVB2D，已单独正确实现。

**根因**: UNPACR 写 SrcA 时未应用 `Haloize_mode`（`THCON_SEC0_REG2` bit 8）。
该标志启用 within-face 16×16 转置：将原本写入 `SrcA[row][col]` 的 datum 改写到 `SrcA[col][row]`，
具体实现为 swap `(row & 0xf)` 与 `col`（行的低 4 位与列互换）。

**架构**:
- BF16 的 WH transpose 完全在 T0（Unpack）完成，T1 只做 MOVA2D 拷贝
- T0 MOP 展开 4 次 UNPACR，每次读一个 face（z=0,2,1,3），每次 `set_dat_valid=1`（FlipSrc）
- 双缓冲流水：每次 UNPACR 写 rows 0..15 到当前 bank，flip bank 后 T1 逐 face 消费
- 所有 4 次调用 `out_addr=0` 是正确的——不需要 SrcRow 累加器或 ADD_DEST_ADDR_CNTR

**修复**:

```c
/* 读 REG2 bit 8 */
bool haloize_mode = (reg2_val >> 8) & 0x1;

/* 写 SrcA 路径，datum i → row=datum_idx/16, col=datum_idx&15 */
if (haloize_mode) {
    uint32_t row_low = row & 0xf;
    uint32_t tmp = row_low; row_low = col; col = tmp;
    row = (row & ~0xfU) | row_low;
}
```

**关键点**: `Haloize_mode` 在 `llk_unpack_A_init_` 中通过
`cfg_reg_rmw_tensix<THCON_SEC0_REG2_Haloize_mode_RMW>(within_face_16x16_transpose)` 写入；
Blackhole cfg_defines 确认：`THCON_SEC0_REG2_Haloize_mode_SHAMT=8`（bit 8，非 bit 10）。
