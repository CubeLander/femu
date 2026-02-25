# Repository Layout Contract

This file defines where code and artifacts should live after the repository became an independent `rv32emu` workspace.

## 1) Source of Truth

1. `rv32emu/` is the source-of-truth project for emulator implementation.
2. All emulator code changes (`include/`, `src/`, `tests/`, `tools/`) must happen under `rv32emu/`.
3. Emulator architecture and implementation docs should prefer `rv32emu/docs/`.

## 2) Workspace Root Responsibilities

1. `scripts/`: orchestration scripts for fetching/building Linux/OpenSBI/rootfs and smoke runs.
2. `out/`: generated artifacts and logs.
3. `docs/`: workspace-level migration notes, logs, and cross-cutting reports.
4. `Makefile`: entrypoint for orchestration and forwarding to `rv32emu/`.

## 2.1) Scripts Split

1. `/workspace/scripts/`: pipeline/orchestration scripts.
2. `/workspace/rv32emu/scripts/`: emulator-local analysis/utility scripts.
3. Do not duplicate the same script behavior in both locations.

## 3) Practical Rules

1. If a change only affects emulator runtime/ISA/JIT/MMIO behavior, patch inside `rv32emu/` only.
2. If a change affects artifact pipelines (toolchain/kernel/rootfs/smoke), patch root `scripts/` and root `Makefile`.
3. Avoid adding new emulator source files at repository root.
4. Use `make -C rv32emu ...` for emulator-local build/test verification.

## 4) Cleanup Direction (Incremental)

1. Keep historical root docs, but place new emulator feature docs in `rv32emu/docs/`.
2. Preserve root-level wrappers for convenience; avoid duplicating logic that already exists in `rv32emu/`.
3. When uncertain, prefer `rv32emu/` as the default location for new technical content.
