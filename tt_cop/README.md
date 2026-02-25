# Tensix Coprocessor Library (tt_cop)

这是 Tensix 协处理器的独立模块，用于与 rv32_emu RISC-V 模拟器集成。

## 目录结构

```
tt_cop/
├── tensix.h           - Tensix 核心结构和接口定义
├── tensix_cop.h       - 协处理器状态机和 FIFO 管理
├── tensix_cop.c       - 协处理器实现（含 Wait Gate 逻辑）
├── tensix_impl.c      - 对外接口实现
├── tt_insn.c          - Tensix 指令跳转表和执行
├── io.h               - 内存接口定义（独立测试用）
├── test_ttcop.c       - 基础功能单元测试
├── test_wait_behavior.c - Wait 指令行为测试
├── Makefile           - 独立编译和测试
└── README.md          - 本文档
```

## 编译和测试

### 快速开始

```bash
cd tt_cop
make           # 编译并运行所有测试
```

### 可用命令

```bash
make build     # 只编译库对象文件
make test      # 编译并运行所有测试
make run-basic # 运行基础功能测试
make run-wait  # 运行 Wait 指令测试
make clean     # 清理编译产物
make rebuild   # 重新编译
make help      # 显示帮助信息
```

## 测试说明

### test_ttcop.c - 基础功能测试

测试内容：
1. **单核执行** - 验证单个 TRISC 核心的指令执行
2. **独立指令流** - 验证三个 TRISC 核心独立运行
3. **Wait 指令隔离** - 验证 Wait 只阻塞当前核心
4. **全核心步进** - 验证 tensix_step_all 功能
5. **FIFO 隔离** - 验证每个核心的 FIFO 独立
6. **信号量操作** - 验证每个核心的信号量独立

### test_wait_behavior.c - Wait 指令行为测试

测试场景：
- TRISC 推送一批指令后遇到 Wait
- 验证 Wait 指令阻塞时 FIFO 保持不动
- 验证条件满足后自动继续执行后续指令
- 验证不需要重新推送 Wait 指令

## 核心接口

### 初始化

```c
void tensix_init(tensix_t *tt,
                 uint8_t *l1_scratchpad_mem,
                 uint8_t *above_ffb,
                 uint8_t *b_ldm,
                 uint8_t *nc_ldm,
                 uint8_t *t0_ldm,
                 uint8_t *t1_ldm,
                 uint8_t *t2_ldm);
```

### 指令推送

```c
bool tensix_push(tensix_t *tt, uint32_t insn, int core_id);
// core_id: 0=T0, 1=T1, 2=T2
// 返回: true=成功, false=FIFO 满
```

### 指令执行

```c
bool tensix_step(tensix_t *tt, int core_id);
// 返回: true=执行了指令, false=Wait 阻塞或 FIFO 空

void tensix_step_all(tensix_t *tt);
// 执行所有三个核心的一条指令
```

### 重置

```c
void tensix_reset(tensix_t *tt);
```

## Wait 指令行为

根据测试验证，Wait 指令的行为如下：

1. **指令锁存** - Wait 指令从 FIFO 取出并锁存到 Wait Gate
2. **阻塞行为** - 阻塞期间，FIFO 中的后续指令保持不动
3. **自动继续** - 条件满足后，自动从 FIFO 取出并执行后续指令
4. **无需重推** - 不需要 RISC-V 核心重新推送 Wait 指令
5. **按需推送** - 只需在 FIFO 空时推送新的工作指令

## 与 rv32_emu 集成

在 rv32_emu 中：
- `tensix.h` 会包含 `rv32emu/src/io.h` 来获取 `memory_t` 定义
- 编译时通过 `-Irv32emu/src -Itt_cop` 确保正确的头文件顺序
- `tensix_init` 将在 QEMU 的 tenstorrent 设备实现中调用
- `tensix_push` 和 `tensix_step` 在 rv32_emu 的 emulate.c 中调用

## 开发和调试

修改 tt_cop 代码后，建议先运行独立测试：

```bash
cd tt_cop
make rebuild
```

确保测试通过后，再在 rv32_emu 层面编译：

```bash
cd ..
make clean
make
```

## 注意事项

1. **memory_t 定义** - 独立编译时使用 `tt_cop/io.h`，集成时使用 `rv32emu/src/io.h`
2. **未使用参数** - `tensix_init` 中的内存参数当前未使用（将来可能扩展）
3. **FIFO 大小** - 每个核心的 FIFO 大小为 32 条指令
4. **核心编号** - T0=0, T1=1, T2=2

## 测试输出示例

成功运行测试时，您会看到：

```
=========================================
Running Tensix Coprocessor Unit Tests
=========================================

=== Test: Single core execution ===
✓ PASS

=== Test: Independent instruction streams ===
✓ PASS

...

║  Passed:  6                           ║
║  Failed:  0                           ║
```

所有测试通过表示 tt_cop 模块工作正常。
