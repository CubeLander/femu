# CPU Execution Pipeline (Draft)

## 1. High-Level Pipeline

Current instruction path can be understood as:

1. Fetch (2 bytes first, to detect compressed vs 32-bit).
2. Decode (`rv32emu_decode32` for 32-bit instructions).
3. Dispatch by opcode group.
4. Execute semantics and update architectural state.
5. Retire (update `pc`, `x0`, `cycle`, `instret`, timer).

Primary entry points:

1. `rv32emu_exec_one` in `src/cpu/rv32emu_cpu_exec.c`.
2. `rv32emu_exec_decoded` in `src/cpu/rv32emu_cpu_exec.c`.
3. Run loop in `src/cpu/rv32emu_cpu_run.c`.

## 2. Decode Stage

For 32-bit instructions, decode output (`rv32emu_decoded_insn_t`) includes:

1. Raw instruction (`raw`).
2. Structural fields (`opcode`, `rd`, `funct3`, `rs1`, `rs2`, `funct7`).
3. Pre-computed immediates (`imm_i/s/b/u/j`).
4. Coarse instruction class (`insn_class`).

Current decoder location:

1. `src/cpu/rv32emu_decode.c`
2. `include/rv32emu_decode.h`

## 3. Execute Grouping (Pedagogical Layout)

The execute path is intentionally split into chapters:

1. `rv32emu_exec_cf_group`: control flow (`lui`, `auipc`, `jal`, `jalr`, branches).
2. `rv32emu_exec_mem_group`: integer/fp loads and stores.
3. `rv32emu_exec_int_group`: OP-IMM/OP integer arithmetic + M extension dispatch.
4. `rv32emu_exec_misc_group`: fence, AMO, FP op, SYSTEM instructions.

Compressed instructions are handled separately by `rv32emu_exec_compressed`.

## 4. Privilege/System Semantics

System details are split into dedicated file:

1. `src/cpu/rv32emu_cpu_exec_system.c`:
   - `rv32emu_exec_csr_op`
   - `rv32emu_exec_mret`
   - `rv32emu_exec_sret`

This separation makes it easier to reason about privilege transitions and CSR side effects.

## 5. Run Loop and Multi-Hart Scheduling

Run loop policies (`src/cpu/rv32emu_cpu_run.c`):

1. Single-thread round-robin across harts.
2. Optional threaded mode via `RV32EMU_EXPERIMENTAL_HART_THREADS=1`.
3. Slice-based fairness (`RV32EMU_HART_SLICE_INSTR`).

Interrupt check occurs before execution attempt each iteration.

## 6. Experimental TB/JIT Path

Execution fast-path controls:

1. `RV32EMU_EXPERIMENTAL_TB=1`: enable decoded-TB stepping.
2. `RV32EMU_EXPERIMENTAL_JIT=1`: enable x86_64 machine-code JIT attempt.
3. `RV32EMU_EXPERIMENTAL_JIT_HOT=<N>`: compile a TB line only after it is seen `N` times (default `3`).
4. `RV32EMU_EXPERIMENTAL_JIT_SKIP_MMODE=1`: do not attempt JIT while hart is in M-mode.
5. `RV32EMU_EXPERIMENTAL_JIT_GUARD=1`: enable conservative no-progress cooldown/fallback guard in run loop.
6. `RV32EMU_EXPERIMENTAL_JIT_DISABLE_ALU=1|..._MEM=1|..._CF=1`: selectively disable JIT opcode classes for triage.

When `RV32EMU_EXPERIMENTAL_JIT=1` is enabled, runner defaults are safety-first:

1. `RV32EMU_EXPERIMENTAL_JIT_SKIP_MMODE` defaults to enabled.
2. `RV32EMU_EXPERIMENTAL_JIT_GUARD` defaults to enabled.

Use explicit `...=0` to override either behavior.

JIT is currently a prefix JIT:

1. Build a decoded TB line.
2. When line hotness reaches threshold, compile a supported straight-line prefix.
3. JIT block epilogue performs direct tail-jump chaining across compatible successor blocks.
4. If JIT path cannot proceed, fallback to decoded-TB/interpreter path.

## 7. Known CPU-Level Gaps

1. TB currently does not cache compressed instruction streams.
2. JIT native coverage is still partial and may require class-level gating for Linux boot stability.
3. Chaining still relies on C helper checks at block boundaries (interrupt/budget/cache), not fully inlined machine-code guards.
4. Coverage/perf instrumentation for TB/JIT hit rate is not yet present.
