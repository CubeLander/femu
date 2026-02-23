# Coverage Gap Analysis (Package D)

依据 `docs/testing/31-coverage-matrix.md` 的已覆盖项，本文件列出当前未覆盖或覆盖不足的测试缺口。

## 1. High Risk Gaps

| Gap ID | Missing coverage | Why risk is high | Proposed addition (concrete name) | Related implementation |
|---|---|---|---|---|
| GAP-H1 | 页表异常场景不完整（无效 PTE、非叶子权限组合、superpage） | 影响地址翻译正确性和 trap 语义，容易引入 silent corruption | 新增 `tests/test_system_vm_faults.c`，用例 `test_sv32_invalid_pte_fault`, `test_sv32_superpage_leaf_translation`, `test_sv32_exec_page_fault` | `src/rv32emu_virt_trap.c` |
| GAP-H2 | 原子指令失败路径覆盖不足（仅覆盖跨 hart 失效与一次成功） | LR/SC 与 AMO 在并发下最容易出现回归 | 在 `tests/test_run.c` 新增 `test_multihart_lr_sc_retry_success`; 在 `tests/test_system.c` 新增 `test_amo_swap_xor_or_and` | `src/rv32emu_cpu_exec.c`, `src/rv32emu_cpu_run.c` |
| GAP-H3 | trap 入口对齐/异常优先级/嵌套场景未验证 | trap 机制是所有异常路径基础，回归影响面大 | 新增 `tests/test_system_trap_edges.c`，用例 `test_mtvec_misaligned_base_behavior`, `test_interrupt_vs_exception_priority`, `test_nested_trap_state_save_restore` | `src/rv32emu_virt_trap.c`, `src/rv32emu_cpu_exec_system.c` |
| GAP-H4 | Loader 异常输入未覆盖（坏 ELF、截断、越界段） | 可导致加载失败路径错误或越界写 | 在 `tests/test_loader.c` 新增 `test_load_elf32_bad_magic`, `test_load_elf32_truncated_segment`, `test_load_elf32_segment_out_of_dram` | `src/rv32emu_stubs.c` |
| GAP-H5 | JIT 精确恢复仍缺少“分支原生翻译+patching”压力（当前已覆盖 first/no-retire/last-boundary/chain/fault-handler-entry/branch-side-exit/multi-trap/jal-jalr-helper） | 当 JIT 从 helper 终止路径升级到原生分支时，边界 patching 与恢复点映射最容易回归 | 在后续 native-branch JIT 实现后补 `test_jit_native_branch_taken_recovery`, `test_jit_native_branch_not_taken_recovery`, `test_jit_native_branch_trap_resume`（覆盖分支原生路径的恢复一致性） | `src/rv32emu_tb.c`, `src/rv32emu_cpu_run.c`, `src/rv32emu_virt_trap.c` |

## 2. Medium Risk Gaps

| Gap ID | Missing coverage | Why risk is medium | Proposed addition (concrete name) | Related implementation |
|---|---|---|---|---|
| GAP-M1 | UART/PLIC 仅覆盖 RX 与 claim/complete，缺少阈值/优先级组合 | 中断可达但仲裁策略回归不易被现有用例发现 | 在 `tests/test_platform.c` 新增 `test_plic_priority_threshold_masking`, `test_uart_irq_disabled_no_claim` | `src/rv32emu_mmio_uart_plic.c` |
| GAP-M2 | CSR 非法访问与权限检查未覆盖（只测合法路径） | CSR 权限 bug 常见且影响系统态安全 | 在 `tests/test_system.c` 新增 `test_csr_illegal_access_trap`, `test_sret_illegal_in_m_mode_state` | `src/rv32emu_csr.c`, `src/rv32emu_cpu_exec_system.c` |
| GAP-M3 | F 扩展仅覆盖数据搬运，缺少算术与舍入模式 | FPU 回归可能在非搬运指令中出现 | 在 `tests/test_system.c` 新增 `test_fp_add_sub_mul_div_single`, `test_fp_rounding_mode_effect` | `src/rv32emu_cpu_exec.c` |
| GAP-M4 | 调度上限与停止条件仅有基础覆盖 | `rv32emu_run(max_instructions)` 边界易出现 off-by-one | 在 `tests/test_run.c` 新增 `test_run_instruction_budget_boundary`, `test_run_zero_budget_no_progress` | `src/rv32emu_cpu_run.c` |

## 3. Low Risk Gaps

| Gap ID | Missing coverage | Why risk is low | Proposed addition (concrete name) | Related implementation |
|---|---|---|---|---|
| GAP-L1 | RAW loader 对大文件/非对齐加载地址无覆盖 | 功能单纯，当前基础路径已覆盖 | 在 `tests/test_loader.c` 新增 `test_load_raw_large_image`, `test_load_raw_unaligned_address` | `src/rv32emu_stubs.c` |
| GAP-L2 | 多 hart 数量>2 的平台路由未覆盖 | 当前 2-hart 基础路由已覆盖，扩展风险相对可控 | 在 `tests/test_platform.c` 新增 `test_clint_msip_three_harts`, `test_plic_meip_three_harts` | `src/rv32emu_mmio_clint_timer.c`, `src/rv32emu_mmio_uart_plic.c` |

## 4. Suggested Execution Order

1. 先做 `GAP-H1/H4`（VM + loader 异常路径）。
2. 再做 `GAP-H2/H3`（原子并发 + trap 边界）。
3. 最后补 `GAP-M*` 与 `GAP-L*`（回归面扩展）。

## 5. Notes

- 本分析仅使用 Package D 指定的四个测试源文件作为“现状覆盖”输入。
- 对“高/中/低风险”的分级依据是对 CPU 正确性、trap 完整性、以及潜在内存破坏影响范围的工程判断。
