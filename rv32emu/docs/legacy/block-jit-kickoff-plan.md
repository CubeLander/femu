# RV32EMU Block JIT Kickoff Plan

## 1. 目标

在不破坏当前 SMP 功能正确性的前提下，逐步引入“块级 JIT 翻译”，优先提升 Linux 启动与内核态热点路径性能。

## 2. 非目标

1. 不做周期精确微架构模拟。
2. 不一次性覆盖全部 RV32 指令集和异常路径。
3. 不在第一阶段引入跨块全局优化。

## 3. 分阶段路线

### Phase 0: 框架与开关

1. 增加运行期开关（例如 `RV32EMU_EXPERIMENTAL_JIT=1`）。
2. 引入 `TB`（translation block）缓存结构：
   - key: `{pc, priv, satp, mstatus.MXR/SUM, mmu_mode}`
   - value: 已翻译块 + 元数据（长度、出口类型、失效标记）。
3. 保留解释器作为 fallback（每个 TB 末尾可回退）。

### Phase 1: 最小可运行 TB

1. 仅支持整数子集（先覆盖启动热点：分支、load/store、算术、CSR 子集）。
2. 单 TB 内顺序执行，块尾通过 dispatcher 选择下一 TB 或回解释器。
3. 异常与中断在块边界检查（先保守，后优化）。

### Phase 2: 内存与 MMU 快路径

1. 为 TB 内常见访存注入页表翻译快路径（TLB 缓存）。
2. MMIO/缺页/权限失败自动走慢路径并退出 TB。
3. satp/mstatus 关键位变化时精确失效 TB。

### Phase 3: 并发与可维护性

1. 按 hart 拥有本地 TB cache，降低锁竞争。
2. 明确 JIT 与 threaded 模式协作边界（禁共享可变 TB 状态）。
3. 增加 JIT/解释器一致性回归（同输入比对寄存器与内存结果）。

## 4. 风险点

1. 异常精确性（fault PC/tval）偏差。
2. TB key 维度不足导致错误复用。
3. MMIO/中断时序与解释器不一致。
4. 多 hart 并发下的 TB 失效竞态。

## 5. 近期建议（下一步）

1. 先完成 Phase 0：只做开关 + TB 数据结构 + 空 dispatcher。
2. 选一个可控子集做 Phase 1（建议先跑用户态小程序，不直接上 Linux）。
3. 建立 JIT 对照测试框架后再扩大覆盖。

