# TB/JIT 实验阶段总结（2026-02-24）

## 1. 本轮目标

本轮从“继续加功能”切换到“可维护性整理 + 阶段总结”，重点是：

1. 降低 `rv32emu_tb.c` 维护成本。
2. 汇总近期 async/template/struct-template 实验的有效结论。
3. 明确下一阶段重构与验证计划。

## 2. 代码整理已完成项

### 2.1 `rv32emu_tb.c` 拆分（低风险机械拆分）

- 将原 `src/tb/rv32emu_tb.c` 中 x86 JIT/TB 大段实现整体抽出到：
  - `src/tb/jit/x86/rv32emu_tb_jit_x86_impl.h`
- 在 `src/tb/rv32emu_tb.c` 原位置替换为：
  - `#include "jit/x86/rv32emu_tb_jit_x86_impl.h"`
- 结果：
  - `rv32emu_tb.c` 行数从 **3579** 降到 **1366**。
  - 语义保持不变（机械搬移，主逻辑未改）。

### 2.2 编译验证

- `make -C rv32emu rv32emu -j$(nproc)` 通过。
- `make -C rv32emu test` 当前失败于：
  - `tests/test_run.c:504`
  - 断言：`result.status == RV32EMU_TB_JIT_HANDLED_NO_RETIRE`
- 该失败更像当前分支已有 JIT 行为问题，不是本次拆文件动作引入的结构性编译问题。

### 2.3 二次细分（x86 JIT 子模块）

- 将 `src/tb/jit/x86/rv32emu_tb_jit_x86_impl.h` 进一步拆成 5 个子文件，并由聚合入口按顺序 include：
  - `src/tb/jit/x86/runtime/rv32emu_tb_jit_x86_runtime_impl.h`
  - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_flow.c`
  - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_lowering.c`
  - `src/tb/jit/x86/rv32emu_tb_jit_x86_compile.c`
  - `src/tb/jit/x86/async/rv32emu_tb_jit_x86_async.c`（后续从 include 聚合迁为真实 `.c`）
  - `src/tb/jit/x86/rv32emu_tb_jit_x86_fallback.c`
- 聚合入口保留 `#if defined(__x86_64__)` 条件编译，子文件不跨文件承载预处理分支。
- 拆分后（阶段二）行数分布：
  - `rv32emu_tb.c`: 1366
  - `rv32emu_tb_jit_x86_runtime_impl.h`: 810
  - `rv32emu_tb_jit_x86_emit_flow.c`: 15
  - `rv32emu_tb_jit_x86_emit_lowering.c`: 25
  - `rv32emu_tb_jit_x86_compile.c`: 295
  - `rv32emu_tb_jit_x86_async.c`: 472
  - `rv32emu_tb_jit_x86_fallback.c`: 23

### 2.4 三次细分（runtime/async 下钻）

- 将 `runtime` 再细分为：
  - `src/tb/jit/x86/runtime/rv32emu_tb_jit_x86_runtime_state.c`
  - `src/tb/jit/x86/runtime/rv32emu_tb_jit_x86_runtime_dispatch.c`
  - `src/tb/jit/x86/runtime/rv32emu_tb_jit_x86_runtime_exec.c`
- `async` 已完成一次 real-TU 迁移，当前为：
  - `src/tb/jit/x86/async/rv32emu_tb_jit_x86_async.c`
  - 旧 `*_async*_impl.h` 已移除
- `runtime_state_impl.h` 已移除，`runtime_impl.h` 仅作为轻量入口占位。
- 当前行数分布（阶段三）：
  - `rv32emu_tb.c`: 1533
  - `rv32emu_tb_jit_x86_impl.h`: 7
  - `rv32emu_tb_jit_x86_runtime_impl.h`: 1
  - `rv32emu_tb_jit_x86_runtime_state.c`: 307
  - `rv32emu_tb_jit_x86_runtime_dispatch.c`: 87
  - `rv32emu_tb_jit_x86_runtime_exec.c`: 310
  - `rv32emu_tb_jit_x86_emit_flow.c`: 121
  - `rv32emu_tb_jit_x86_emit_lowering.c`: 173
  - `rv32emu_tb_jit_x86_emit_primitives_base_impl.h`: 38
  - `rv32emu_tb_jit_x86_emit_primitives_alu_impl.h`: 96
  - `rv32emu_tb_jit_x86_emit_primitives_helper_impl.h`: 95
  - `rv32emu_tb_jit_x86_emit_primitives_impl.h`: 4
  - `rv32emu_tb_jit_x86_emit_lowering_alu_impl.h`: 186
  - `rv32emu_tb_jit_x86_emit_lowering_mem_impl.h`: 16
  - `rv32emu_tb_jit_x86_emit_lowering_cf_impl.h`: 16
  - `rv32emu_tb_jit_x86_compile.c`: 290
  - `rv32emu_tb_jit_x86_async.c`: 472
  - `rv32emu_tb_jit_x86_fallback.c`: 23

### 2.5 四次细分（internal 头与 lowering 子模块）

- 新增私有内部头用于后续 `.c` 化拆分：
  - `src/internal/tb_internal.h`
  - `src/internal/tb_jit_internal.h`
- `emit_lowering` 按指令族拆为 3 个子文件，由 `emit_lowering.c` 直接编译聚合：
  - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_lowering_alu_impl.h`
  - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_lowering_mem_impl.h`
  - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_lowering_cf_impl.h`
- 该阶段保持“零语义变化”，验证结果仍与基线一致：
  - `make -C rv32emu rv32emu` 通过
  - `make -C rv32emu test` 仍停在 `tests/test_run.c:504` 已知断言

## 3. 实验数据摘要（重点：600m + struct-template2）

说明：以下均来自 `out/smoke/` 同批命名日志，平台为同一台机器；该批数据存在可见波动，需要多次重复取中位数。

| 场景 | real time | retire_rate | retired_insns | template_hits | struct_hits | template_applied |
|---|---:|---:|---:|---:|---:|---:|
| `...-fix3-600m` | 59.695s | 13.41% | 2,368,763 | - | - | - |
| `...-template1-600m-rerun` | 51.417s | 14.57% | 2,443,558 | 556 | - | - |
| `...-structtemplate2-600m` | 54.811s | 15.20% | 2,420,568 | 684 | 134 | 0 |
| `...-structtemplate2-600m-rerun` | 56.356s | 14.14% | 2,353,077 | 745 | 155 | 0 |
| `...-template2-frontapply-600m` | 55.579s | 24.64% | 3,913,324 | 25,431 | - | 25,431 |

补充探针（`probe-async-structtemplate2-200m.log`）：

- `retire_rate=30.19%`
- `template_hits=643`
- `struct_hits=145`
- `stale=0`

## 4. 当前结论

1. **模板复用路径是有效的**：`template_hits`、`struct_hits` 持续非零，且 async `stale` 基本为 0。
2. **结构模板（struct-template）已工作，但收益尚不稳定**：600m 两次分别 54.811s/56.356s，仍受波动影响。
3. **front-apply 能显著提高 retire_rate，但 wall time 未同步线性下降**：说明仍有其他瓶颈（编译/内存池/调度开销）。
4. **`fail_alloc` 量级偏高（~6万）**：JIT pool 压力仍明显，是下一步优化重点。

## 5. 下一阶段建议（先整理后提速）

1. 进入下一阶段：基于 `src/internal/tb_internal.h` 与 `src/internal/tb_jit_internal.h`，开始把 x86 `*_impl.h` 聚合逐步转为真实 `.c` 编译单元，并补齐子模块回归测试。
2. 给实验脚本增加“固定场景 + N 次重复 + 中位数/分位数”输出，避免单次 run 误导。
3. 优先处理 `fail_alloc`：
   - 降低无效编译尝试；
   - 提高复用命中后的“零分配”路径覆盖；
   - 评估分层池或回收策略。
4. 补一组“功能正确性优先”回归，锁定 `test_jit_fault_first_insn_no_retire` 的行为基线。

## 6. 参考日志

- `out/smoke/jit-mid-async-bg-prefetch1-discount0-recycle1-fallback8-pool4mb-drain8-fix3-600m.log`
- `out/smoke/jit-mid-async-bg-prefetch1-discount0-recycle1-fallback8-pool4mb-drain8-template1-600m-rerun.log`
- `out/smoke/jit-mid-async-bg-prefetch1-discount0-recycle1-fallback8-pool4mb-drain8-structtemplate2-600m.log`
- `out/smoke/jit-mid-async-bg-prefetch1-discount0-recycle1-fallback8-pool4mb-drain8-structtemplate2-600m-rerun.log`
- `out/smoke/jit-mid-async-bg-prefetch1-discount0-recycle1-fallback8-pool4mb-drain8-template2-frontapply-600m.log`
- `out/smoke/probe-async-structtemplate2-200m.log`
