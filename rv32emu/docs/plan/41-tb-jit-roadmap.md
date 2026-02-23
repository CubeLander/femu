# TB/JIT Architecture Roadmap

## 1. Current TB/JIT Design (Code Truth)

Primary code paths:

1. `src/rv32emu_tb.c`
2. `include/rv32emu_tb.h`
3. `src/rv32emu_cpu_run.c`
4. `src/rv32emu_cpu_exec.c`

Current implementation has both:

1. Decoded-TB cache/step path.
2. Experimental x86_64 machine-code JIT prefix path for a subset of integer ALU instructions.
3. Direct tail-jump chaining between compatible JIT blocks (with helper-assisted boundary checks).

### 1.1 Cache Structure

1. TB cache is per-hart runtime state (`rv32emu_tb_cache_t`) with:
   - 256 direct-mapped lines (`RV32EMU_TB_LINES`, `include/rv32emu_tb.h`).
   - Up to 32 decoded instructions per line (`RV32EMU_TB_MAX_INSNS`).
2. Each line stores:
   - `start_pc`
   - `pcs[]` (per instruction PC)
   - `decoded[]` (`rv32emu_decoded_insn_t` array)
3. Cache line index is `(pc >> 2) & (RV32EMU_TB_LINES - 1)` (`rv32emu_tb_index` in `src/rv32emu_tb.c`).

### 1.2 Block Build Policy

`rv32emu_tb_build_line` builds a line from `start_pc` forward and stops on first unsupported boundary:

1. Stop if PC is odd (`pc & 1 != 0`).
2. Fetch 16-bit prefix; stop if fetch fails.
3. Stop if instruction is compressed (`(insn16 & 0x3) != 0x3`).
4. Fetch 32-bit instruction; stop if fetch fails.
5. Decode via `rv32emu_decode32` and append.
6. Stop after block terminator opcode:
   - `0x63` branch
   - `0x67` `jalr`
   - `0x6f` `jal`
   - `0x73` `system`
7. Stop at 32 instructions max.

If zero instructions were appended, build fails (`false`).

### 1.3 Execute Policy

`rv32emu_exec_one_tb` executes exactly one decoded instruction per call:

1. If there is an active line/index and `pc` matches expected `pcs[active_index]`, reuse active state.
2. Otherwise lookup/build from current `pc`.
3. Execute with shared interpreter semantic engine `rv32emu_exec_decoded`.
4. If execution succeeds and `next pc == pcs[index+1]`, remain active and increment index.
5. Otherwise clear active state (side-exit) and return success for this instruction.
6. If execute fails, clear active state and return `false`.

## 2. Correctness Invariants and Side-Exit Rules

### 2.1 Correctness Invariants

1. Semantic source of truth is single path `rv32emu_exec_decoded` (`src/rv32emu_cpu_exec.c`), used by both TB and non-TB paths.
2. Architectural state update remains centralized in `rv32emu_exec_decoded`:
   - PC commit
   - `x0` hardwire reset
   - `cycle`/`instret` increment
   - timer step
3. TB mode never bypasses interrupt polling: run loops call `rv32emu_check_pending_interrupt` before TB dispatch (`src/rv32emu_cpu_run.c`).
4. TB execution granularity is one instruction per outer loop iteration; no multi-instruction atomic commit.

### 2.2 Side-Exit Rules (Current)

A side-exit (active TB discontinuity) happens when any of the following is true:

1. Runtime PC no longer matches expected `pcs[active_index]`.
2. Post-exec PC does not match `pcs[index + 1]`.
3. Current decoded instruction execution returns false.
4. Initial lookup/build for current PC fails.

Result policy:

1. Side-exit deactivates current active TB state (`cache->active = false`).
2. If instruction already executed successfully in this call, function still returns `true`.
3. Only a hard failure (`false`) triggers runner-level fallback to `rv32emu_exec_one`.

## 3. Unsupported Instruction Classes and Fallback Policy

### 3.1 Currently Unsupported for TB Build/Continuation

1. Compressed (RVC, 16-bit) instructions are not inserted into TB lines.
2. Entry PC with odd alignment is not inserted into TB lines.
3. Any fetch-faulted instruction cannot be inserted.
4. Cross-control-flow continuation is not kept active after branch/jump/system terminators.

### 3.2 Runner Fallback Policy

In both single-thread and threaded loops (`src/rv32emu_cpu_run.c`):

1. TB mode is opt-in via `RV32EMU_EXPERIMENTAL_TB=1`.
2. When TB dispatch returns `false` and hart is still running, runner retries same step via `rv32emu_exec_one`.
3. When TB is disabled, runner always uses `rv32emu_exec_one`.
4. Each worker/hart owns a private TB cache object; cache is reset before execution.

## 4. Staged Path to Machine-Code JIT Backend

Stages below are incremental and measurable; each stage must preserve Section 2 invariants.

### Stage 0: Baseline Hardening of Decoded-TB (now)

Scope:

1. Keep current decoded-TB behavior as the reference baseline.
2. Add TB counters (build attempts, build hits, side-exits, interpreter fallbacks).

Milestones:

1. `RV32EMU_EXPERIMENTAL_TB=1` passes existing test suite parity with TB off.
2. Emit per-run counters to verify TB path is actually exercised.

### Stage 1: Safe Invalidation + Coherency

Scope:

1. Add TB invalidation on guest code writes (at least per-line coarse invalidation by PC range/index).
2. Define explicit policy for self-modifying code and `fence.i` handling.

Milestones:

1. New tests demonstrate modified code is re-fetched/re-decoded before execution.
2. No stale-decoded execution under directed self-modifying micro-tests.

### Stage 2: Internal JIT IR (Non-native)

Scope:

1. Introduce a compact internal IR per TB line (typed ops for ALU/load/store/branch/system).
2. Keep execution through interpreter backend generated from IR (no native code yet).

Milestones:

1. IR generation coverage >= 90% of dynamic instructions on target benchmark set.
2. IR-exec and decode-exec produce identical architectural traces on regression set.

### Stage 3: Machine-Code JIT for Straight-Line Subset

Scope:

1. Add host machine-code emission for straight-line, non-privileged, non-trapping hot blocks.
2. Keep guard checks and deterministic side-exit to decoded interpreter.

Milestones:

1. Native backend executes selected subset with zero architectural mismatches in differential runs.
2. Measured speedup target: >= 1.5x on TB-friendly microbenchmarks versus Stage 0 TB baseline.

### Stage 4: Control-Flow and Privileged Expansion

Scope:

1. Extend native backend to guarded branch handling and limited block chaining.
2. Add privilege/trap-sensitive bailout stubs for system and MMU-sensitive operations.

Milestones:

1. All current TB terminator classes have explicit native-side bailout stubs.
2. Differential testing across interrupt/trap-heavy workloads remains mismatch-free.

### Stage 5: Default-On Readiness

Scope:

1. Stabilize observability, debug controls, and deterministic fallback semantics.
2. Decide promotion criteria for enabling JIT by default.

Milestones:

1. Long-run stress + multi-hart threaded runs show no regression versus interpreter reference.
2. Documented operational controls (`env` flags, counters, disable switches) are complete.

## 5. Risks to Track During Roadmap

1. Stale decode/JIT due to missing code-write invalidation.
2. Cross-hart memory visibility and invalidation ordering when threaded execution is enabled.
3. Trap/interrupt boundary correctness when introducing multi-instruction native execution.
4. Host ABI/register allocator bugs in machine-code backend.

## 6. Non-goals (Current Package)

1. This roadmap does not claim full-instruction machine-code coverage.
2. This document does not redefine ISA semantics; it depends on `rv32emu_exec_decoded` behavior.
