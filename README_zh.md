# tenstorrent-rv32-emu

[English](README.md)

RV32IMC 指令集模拟器，集成 Tenstorrent Tensix 协处理器支持。编译为共享库（`librv32sim.so`），供 [tenstorrent-qemu-update](https://github.com/xxx/tenstorrent-qemu-update) QEMU 设备使用。

## 功能

### RV32IMC 模拟器
- 基于 [rv32emu](https://github.com/sysprog21/rv32emu)
- RV32I 基础指令集 + M（乘除法）+ C（压缩指令）扩展
- 自定义 TTINSN 扩展指令，用于 Tensix 协处理器交互
- 共享库接口：创建、运行、停止、销毁 CPU 实例

### Tensix 协处理器
- **3 线程执行**：T0（unpackr）、T1（math）、T2（packr）
- **Wait Gate 同步机制**：STALLWAIT / SEMWAIT / STREAMWAIT
- **MOP 展开器**：模板 0（掩码迭代）、模板 1（嵌套循环）
- **约 200 条指令**：算术、数据搬运、同步控制
- **8 个信号量 + 8 个互斥量**（每个协处理器）
- **dvalid 同步**：线程间数据依赖跟踪
- **指令 FIFO**：每线程 32 条目队列

## 编译

```bash
# 克隆仓库
git clone https://github.com/xxx/tenstorrent-rv32-emu.git rv32_emu
cd rv32_emu

# 克隆 rv32emu 基础模拟器
git clone https://github.com/sysprog21/rv32emu.git

# 编译 softfloat（必需依赖）
cd rv32emu/src/softfloat/build/Linux-x86_64-GCC
make -j$(nproc)
cd ../../../../..

# 编译共享库
make -j$(nproc)
```

编译产物为 `librv32sim.so`。

## 目录结构

```
rv32_emu/
├── rv32sim.c/h          # 共享库接口（创建/运行/停止 CPU）
├── Makefile             # 编译配置
├── rv32emu/             # 基础 RV32IMC 模拟器（git clone 获取）
│   └── src/
│       ├── riscv.c/h    # 核心模拟器
│       ├── decode.c     # 指令解码
│       ├── emulate.c    # 指令执行
│       └── softfloat/   # 软浮点库
└── tt_cop/              # Tensix 协处理器
    ├── tensix_cop.c/h   # COP 核心：Wait Gate、MOP、step 逻辑
    ├── tensix_impl.c    # 指令实现
    ├── tensix.h         # Tensix 常量和定义
    ├── tt_insn.c        # TTINSN 自定义指令处理
    ├── test_ttcop.c     # COP 单元测试
    └── test_wait_behavior.c  # Wait Gate 行为测试
```

## API

```c
// 创建 CPU 实例，传入内存和回调
void* rv32sim_create(void* mem, uint32_t mem_size, ...);

// 运行直到停止或协程让出
void rv32sim_run(void* cpu);

// 协程模式运行（协作式多任务）
void rv32sim_run_coroutine(void* cpu);

// 停止正在运行的 CPU
void rv32sim_halt(void* cpu);

// 销毁 CPU 实例
void rv32sim_destroy(void* cpu);
```

## Tensix 节点架构

Blackhole 芯片中每个 Tensix 节点包含 7 个 RV32IMC 核心：

| 核心 | 角色 | 说明 |
|------|------|------|
| B_CORE | 启动/Brisc | 引导和控制 |
| N_CORE | 网络 | NOC 数据搬运 |
| T0 | Unpackr 线程 | 数据解包，输出给 T1 |
| T1 | Math 线程 | 计算操作 |
| T2 | Packr 线程 | 数据打包，消费 T1 输出 |
| DMA_R | DMA 读 | 内存读引擎 |
| DMA_W | DMA 写 | 内存写引擎 |

T0/T1/T2 共享一个 Tensix 协处理器，通过 dvalid 机制实现同步。

## 许可

本项目用于研究和教育目的。
