# RV32EMU SMP Threaded Memory Semantics Checklist

## 1. 目标

本清单用于约束 `RV32EMU_EXPERIMENTAL_HART_THREADS=1`（每个 hart 一个 host 线程）模式下的最小正确性要求，避免 host 并发 UB，同时保证 Linux SMP 启动链路稳定。

## 2. 范围

1. 覆盖 DRAM 普通 load/store、LR/SC、AMO、fence 与 MMIO 顺序性。
2. 不覆盖周期精确缓存协议（MESI/MOESI）建模。
3. 允许比 RVWMO 更强的实现（更保守的顺序），不允许弱于架构要求。

## 3. 必须语义

1. 同地址一致性（coherence）
单个 DRAM 地址的并发写必须在所有 hart 上体现一致的全局顺序。

2. 常规访存原子性（aligned）
对齐 `16/32-bit` 访问不得发生撕裂读写。

3. LR/SC 语义
`lr.w` 建立 reservation；任意 hart 对保留字重叠写入后，后续 `sc.w` 必须失败。

4. AMO 原子性
AMO 读-改-写必须在并发下体现单一原子临界区。

5. fence/aq/rl 语义
需要按架构要求提供顺序保证（可以更强，不能更弱）。

6. MMIO 顺序性
MMIO 访问保序，不与 DRAM fast path 混淆。

7. Host 无 UB
多线程可并发触达的内存位置，必须统一通过 lock 或 atomic 路径访问，禁止同址原子与非原子混用。

## 4. 当前实现映射

1. Threaded DRAM 原子混合路径
`rv32emu/src/rv32emu_memory_mmio.c`

2. LR/SC 跨 hart 失效
`rv32emu/src/rv32emu_virt_trap.c`

3. AMO 原子临界区
`rv32emu/src/rv32emu_cpu_exec.c`

4. MIP 并发读写原子化
`rv32emu/include/rv32emu.h`
`rv32emu/src/rv32emu_csr.c`
`rv32emu/src/rv32emu_memory_mmio.c`
`rv32emu/src/rv32emu_stubs.c`
`rv32emu/src/rv32emu_virt_trap.c`

## 5. 验收清单

1. 单元测试
`make rv32emu-test`

2. 基线双核严格 smoke（单线程调度）
`make smoke-emulator-smp-linux`

3. Threaded 双核严格 smoke
`make smoke-emulator-smp-linux-threaded`

4. 可选统计观测（只做分析，不做 pass/fail）
`RV32EMU_DEBUG_DRAM_STATS=1 make smoke-emulator-smp-linux-threaded`

## 6. Debug 统计口径说明

当前 DRAM 原子路径统计：

1. `aligned32`
对齐 32-bit 单次 relaxed atomic 访问计数。

2. `aligned16`
对齐 16-bit 单次 relaxed atomic 访问计数。

3. `bytepath`
字节粒度原子拼装路径（1-byte 访问与未对齐访问）。

统计输出位于程序退出阶段，见：
`rv32emu/tools/rv32emu_main.c`

