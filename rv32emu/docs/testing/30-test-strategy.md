# Test Strategy and Coverage Draft

## 1. Goals

Testing is organized around three guarantees:

1. ISA-visible correctness (registers, privilege, traps, memory effects).
2. Platform-visible correctness (UART/CLINT/PLIC/SBI behavior).
3. Regression safety for refactors (decode split, TB introduction, run-loop changes).

## 2. Existing Test Suites

### 2.1 `tests/test_platform.c`

Focus:

1. DRAM physical and virtual read/write correctness.
2. UART RX behavior and line status handling.
3. CLINT software/timer interrupt behavior.
4. PLIC enable/claim/complete pathways.
5. Multi-hart interrupt routing sanity.

### 2.2 `tests/test_run.c`

Focus:

1. Base RV32 instruction execution and retirement count.
2. Compressed-instruction basic flow.
3. Multi-hart scheduling fairness sanity.
4. Cross-hart LR/SC invalidation behavior.
5. Experimental JIT integer ALU correctness under `RV32EMU_EXPERIMENTAL_JIT=1`.
6. JIT instruction-budget boundary (no overshoot of `max_instructions`).
7. JIT load/store helper path basic correctness (`sw/lw`).
8. JIT fault path with partial retire behavior (fault instruction not retired).
9. JIT fault-at-first-insn path returns handled-no-retire (no false retire).
10. JIT interrupt-at-entry path returns handled-no-retire with trap PC handoff.
11. JIT fault-at-block-boundary path retires exact prefix count.
12. JIT 链式执行下的中断 trap/mret 恢复（跨 block）保持 PC 与 retire 一致。
13. JIT 跨 block fault 进入 trap handler 后，`mepc` 修正与恢复执行路径正确。
14. JIT 到解释器的 branch side-exit 路径可正确恢复并继续执行。
15. JIT 链式执行下多次 trap/mret 恢复可保持寄存器与 `mepc` 一致性。
16. JIT `jal/jalr` helper 终止路径保持返回地址与跳转目标一致。

### 2.3 `tests/test_system.c`

Focus:

1. SBI shim behavior across major extension IDs.
2. CSR/system instruction behavior.
3. Trap/delegation and page-translation behavior.
4. FP/AMO and selected advanced execution paths.

### 2.4 `tests/test_loader.c`

Focus:

1. Raw image load.
2. ELF32 segment loading.
3. Auto-detect loader behavior.

## 3. Current Coverage Snapshot (Qualitative)

Covered well:

1. Basic RV32 integer execution path.
2. Core privilege and trap operations.
3. Platform MMIO behaviors.
4. SBI + loader integration paths.

Under-covered areas:

1. TB cache hit/miss behavior and fallback edge cases.
2. Long-running randomized instruction streams.
3. Stress tests for threaded multi-hart mode.
4. Fault-injection around translation and MMIO boundaries.

## 4. Recommended New Test Buckets

1. `test_tb.c`
   - TB build, reuse, and fallback correctness.
   - Mix 32-bit and compressed code regions.
2. `test_decode.c`
   - Field/immediate decode oracle checks per instruction format.
3. `test_interrupt_races.c`
   - Timer and external interrupt timing under multi-hart execution.
4. `test_self_modify.c`
   - Required for future machine-code JIT invalidation behavior.

## 5. TB-Specific Regression Checklist

When TB/JIT logic changes, verify:

1. `RV32EMU_EXPERIMENTAL_TB=0` baseline still passes all tests.
2. `RV32EMU_EXPERIMENTAL_TB=1` passes all tests.
3. Behavior at ebreak/ecall/trap boundaries is unchanged.
4. Multihart + LR/SC tests still pass.
5. `RV32EMU_EXPERIMENTAL_JIT=1` and `RV32EMU_EXPERIMENTAL_JIT_HOT=1` pass `tests/test_run.c`.
6. JIT handled-no-retire path does not force immediate interpreter fallback in run loop.

## 6. Performance Validation Template

For each optimization proposal:

1. Record command lines and machine details.
2. Compare baseline vs feature-enabled throughput.
3. Confirm no correctness regressions in full test suite.
4. Keep at least one deterministic microbenchmark for trend tracking.
