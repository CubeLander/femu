# Source Layout Refactor Plan (No `.inc` End State)

## 1. Goal

Clean up `src/` so TB/JIT code is maintainable without `.inc` aggregation, while preserving current runtime behavior and benchmark comparability.

## 2. Non-goals

1. No semantic changes to ISA/MMU/trap behavior in this refactor track.
2. No immediate performance tuning in this track.
3. No public API break in `include/*.h`.

## 3. Current Constraints

1. Build now collects recursive sources under `src/**.c` (via `find src -name '*.c'` in `Makefile`).
2. TB/JIT x86 internals are currently in mixed mode: part `*_impl.h` aggregation, part real `.c` translation units (`runtime_dispatch`, `runtime_exec`, `async`).
3. Existing known baseline test issue remains:
   - `tests/test_run.c:504`
   - `result.status == RV32EMU_TB_JIT_HANDLED_NO_RETIRE`

## 4. Target Layout

```text
src/
  cpu/
    exec.c
    exec_system.c
    run.c
    csr.c
    decode.c
    virt_trap.c
  memory/
    memory_mmio.c
    mmio_devices.c
    mmio_uart_plic.c
    mmio_clint_timer.c
  platform/
    platform.c
    stubs.c
  tb/
    tb_cache.c
    tb_dispatch.c
    tb_stats.c
    jit/
      jit_stub.c
      x86/
        state.c
        helpers.c
        emitter.c
        compiler.c
        async.c
  internal/
    tb_internal.h
    tb_jit_internal.h
```

Notes:

1. Public ABI remains in `include/`.
2. `src/internal/*.h` are private, not installed headers.

## 5. Phased Migration

## Phase 0: Guardrails

Tasks:

1. Freeze baseline metrics and known-failing test signature.
2. Add a short refactor checklist in docs and CI script comments.

Exit criteria:

1. `make -C rv32emu rv32emu` passes.
2. Known failing test remains identical (no new failures introduced).

## Phase 1: Build System Readiness

Tasks:

1. Make `Makefile` discover recursive sources under `src/**.c`.
2. Update object rule to create nested `build/` directories (`mkdir -p $(dir $@)`).

Exit criteria:

1. Build works with files moved into subdirectories.
2. No object name collisions.

## Phase 2: Remove `.inc` in TB/JIT

Tasks:

1. Introduce private headers:
   - `src/internal/tb_internal.h`
   - `src/internal/tb_jit_internal.h`
2. Convert current `.inc` groups to real `.c` translation units:
   - `runtime_*` -> `state.c`, `helpers.c`
   - `emit_*` -> `emitter.c`
   - `compile` -> `compiler.c`
   - `async_*` -> `async.c`
   - fallback -> `jit_stub.c`
3. Keep symbol visibility explicit:
   - file-local helpers stay `static`
   - cross-file internal APIs declared in private headers only

Exit criteria:

1. No `.inc` files referenced from `src/` compile path.
2. Build + tests baseline-equivalent.
3. JIT smoke logs show no structural regression (`retire_rate`, `hit_rate`, `stale` fields present and sane).

## Phase 3: Broader `src/` Domain Reorganization

Tasks:

1. Move non-TB files into `cpu/`, `memory/`, `platform/` groups.
2. Keep file names stable where possible to reduce diff noise.
3. Update include paths and internal references.

Exit criteria:

1. Top-level `src/` mostly directory hubs, not a flat file dump.
2. `git grep` discoverability improved by domain.

## Phase 4: Documentation and Ownership

Tasks:

1. Add `docs/encyclopedia` cross-links for new private module boundaries.
2. Add ownership notes in file headers for high-churn modules (TB/JIT).

Exit criteria:

1. New contributors can locate TB/JIT compile flow in <= 3 files from docs.

## 6. Suggested PR Slicing

1. PR-A: Makefile recursive source support + no code moves.
2. PR-B: TB/JIT `.inc` -> `.c/.h` (no logic changes).
3. PR-C: Remaining `src/` domain moves.
4. PR-D: Doc synchronization and cleanup.

This keeps each PR reviewable and easy to bisect.

## 7. Risk Control

1. Keep each phase behavior-preserving; forbid opportunistic optimization changes in same PR.
2. Use one smoke command set per PR to validate boot path and JIT stats format.
3. If mismatch appears, rollback by PR boundary, not by ad-hoc file reverts.

## 8. Immediate Next Step

Continue **PR-B**: use `src/internal/tb_internal.h` and `src/internal/tb_jit_internal.h` as shared ABI, then convert x86 `*_impl.h` aggregation into real `.c` units without behavior changes.

## 9. Progress

As of 2026-02-24:

1. PR-A is completed locally:
   - `Makefile` source discovery switched from `src/*.c` to recursive `find src -name '*.c'`.
   - object compile rule now ensures nested output dirs via `mkdir -p $(dir $@)`.
2. Validation outcome:
   - `make -C rv32emu rv32emu` passes.
   - `make -C rv32emu test` remains at the same known baseline failure in `tests/test_run.c:504`.
3. TB/JIT source tree has been moved under domain directories (behavior-preserving move):
   - `src/tb/rv32emu_tb.c`
   - `src/tb/jit/x86/**/*`
4. TB/JIT x86 include aggregation cleanup is done for file extension and layering:
   - old `.inc` files renamed to private `*_impl.h`
   - x86 internals regrouped by role:
     - `src/tb/jit/x86/runtime/*`
     - `src/tb/jit/x86/emit/*`
     - `src/tb/jit/x86/async/*`
5. Other runtime sources have been grouped by domain as an intermediate step:
   - `src/cpu/*.c`
   - `src/memory/*.c`
   - `src/platform/*.c`
6. Validation after each refactor step remains baseline-equivalent:
   - `make -C rv32emu rv32emu` passes
   - `make -C rv32emu test` remains at the same known failure in `tests/test_run.c:504`
7. Private internal headers have been introduced and wired into current build:
   - `src/internal/tb_internal.h`
   - `src/internal/tb_jit_internal.h`
8. `emit_lowering` has been split by instruction family (still single-TU include mode):
   - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_lowering_alu_impl.h`
   - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_lowering_mem_impl.h`
   - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_lowering_cf_impl.h`
9. Runtime path has started real translation-unit migration:
   - moved from include-time bodies to compiled units:
     - `src/tb/jit/x86/runtime/rv32emu_tb_jit_x86_runtime_dispatch.c`
     - `src/tb/jit/x86/runtime/rv32emu_tb_jit_x86_runtime_exec.c`
     - `src/tb/jit/x86/runtime/rv32emu_tb_jit_x86_runtime_state.c`
   - old runtime state include body was removed after migration:
     - `rv32emu_tb_jit_x86_runtime_state_impl.h`
10. Async path has started real translation-unit migration:
   - moved from include-time bodies to compiled unit:
     - `src/tb/jit/x86/async/rv32emu_tb_jit_x86_async.c`
   - old async split headers were removed after migration:
     - `rv32emu_tb_jit_x86_async_impl.h`
     - `rv32emu_tb_jit_x86_async_queue_impl.h`
     - `rv32emu_tb_jit_x86_async_apply_impl.h`
     - `rv32emu_tb_jit_x86_async_submit_impl.h`
11. Non-x86 fallback path has been moved to a compiled unit:
   - added:
     - `src/tb/jit/x86/rv32emu_tb_jit_x86_fallback.c`
   - removed:
     - `rv32emu_tb_jit_x86_fallback_impl.h`
12. Foreground compile path has been moved to a compiled unit:
   - added:
     - `src/tb/jit/x86/rv32emu_tb_jit_x86_compile.c`
   - removed:
     - `rv32emu_tb_jit_x86_compile_impl.h`
   - x86 aggregate no longer includes emitter/compile include-time bodies:
     - compile/emission dependencies are now consumed directly by `compile.c`
13. Emitter path has been moved to compiled units:
   - added:
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_flow.c`
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_lowering.c`
   - removed:
     - `src/tb/jit/x86/rv32emu_tb_jit_x86_emit.c`
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_impl.h`
   - `compile.c` now calls emitter APIs via internal header declarations
   - x86 aggregate remains runtime-only include, with emit no longer included from `rv32emu_tb.c`
14. Emitter impl headers have been reduced:
   - removed:
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_flow_impl.h`
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_lowering_impl.h`
   - flow/lowering logic now resides directly in:
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_flow.c`
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_lowering.c`
15. x86 runtime aggregate shim has been fully removed:
   - `src/tb/rv32emu_tb.c` no longer includes `jit/x86/rv32emu_tb_jit_x86_impl.h`
   - removed empty transitional headers:
     - `src/tb/jit/x86/rv32emu_tb_jit_x86_impl.h`
     - `src/tb/jit/x86/runtime/rv32emu_tb_jit_x86_runtime_impl.h`
   - validation remains baseline-equivalent:
     - `make -C rv32emu rv32emu` passes
     - `make -C rv32emu test` remains at the same known failure in `tests/test_run.c:504`
16. `emit_lowering` family include-time bodies have been collapsed into one TU:
   - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_lowering.c` now directly contains ALU/MEM/CF helper bodies
   - removed:
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_lowering_alu_impl.h`
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_lowering_mem_impl.h`
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_lowering_cf_impl.h`
   - validation remains baseline-equivalent:
     - `make -C rv32emu rv32emu` passes
     - `make -C rv32emu test` remains at the same known failure in `tests/test_run.c:504`
17. x86 emit primitives are now a normal module (no include-time implementation headers):
   - added:
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_primitives.h`
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_primitives.c`
   - updated consumers:
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_flow.c`
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_lowering.c`
   - removed:
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_primitives_base_impl.h`
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_primitives_alu_impl.h`
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_primitives_helper_impl.h`
     - `src/tb/jit/x86/emit/rv32emu_tb_jit_x86_emit_primitives_impl.h`
   - source tree now has no remaining `.inc` or `*_impl.h` files under `src/`
   - validation remains baseline-equivalent:
     - `make -C rv32emu rv32emu` passes
     - `make -C rv32emu test` remains at the same known failure in `tests/test_run.c:504`
18. TB JIT config/env helpers were extracted from `rv32emu_tb.c` into a dedicated module:
   - added:
     - `src/tb/rv32emu_tb_jit_config.c`
   - internal interfaces added to:
     - `src/internal/tb_internal.h`
   - moved out from `rv32emu_tb.c`:
     - env parsers (`rv32emu_tb_env_bool`, `rv32emu_tb_u32_from_env`)
     - JIT config readers (`*_from_env`)
     - JIT generation seed helper (`rv32emu_tb_next_jit_generation`)
   - size effect:
     - `src/tb/rv32emu_tb.c` reduced to ~1368 LOC
   - validation remains baseline-equivalent:
     - `make -C rv32emu rv32emu` passes
     - `make -C rv32emu test` remains at the same known failure in `tests/test_run.c:504`
19. TB JIT stats subsystem was extracted from `rv32emu_tb.c` into a dedicated module:
   - added:
     - `src/tb/rv32emu_tb_jit_stats.c`
   - internal contract moved to shared private header:
     - `src/internal/tb_internal.h` now owns `rv32emu_jit_stats_t`, stats globals, and stats macros
   - moved out from `rv32emu_tb.c`:
     - stats storage + enable gate (`g_rv32emu_jit_stats`, `g_rv32emu_jit_stats_mode`, `rv32emu_jit_stats_enabled`)
     - stats APIs (`rv32emu_jit_stats_inc_event`, `rv32emu_jit_stats_add_compile_prefix_insns`,
       `rv32emu_jit_stats_inc_helper_mem_calls`, `rv32emu_jit_stats_inc_helper_cf_calls`,
       `rv32emu_jit_stats_reset`, `rv32emu_jit_stats_dump`)
   - size effect:
     - `src/tb/rv32emu_tb.c` reduced further to ~906 LOC
   - validation remains baseline-equivalent:
     - `make -C rv32emu rv32emu` passes
     - `make -C rv32emu test` remains at the same known failure in `tests/test_run.c:504`
20. x86 JIT/TB bridge wrappers were extracted from `rv32emu_tb.c`:
   - added:
     - `src/tb/rv32emu_tb_jit_x86_bridge.c`
   - moved out from `rv32emu_tb.c`:
     - x86-facing bridge/public wrapper APIs (`*_public`, async env bridge, pool/gen bridge)
   - internal cache lookup API formalized for bridge usage:
     - `rv32emu_tb_find_cached_line` is now declared in `src/internal/tb_internal.h`
   - size effect:
     - `src/tb/rv32emu_tb.c` reduced further to ~811 LOC
   - validation remains baseline-equivalent:
     - `make -C rv32emu rv32emu` passes
     - `make -C rv32emu test` remains at the same known failure in `tests/test_run.c:504`
