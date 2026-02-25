# Workspace Scripts

This directory contains **workspace-level orchestration scripts**.

## Scope

1. Fetch/build external dependencies and artifacts:
   - Linux kernel
   - OpenSBI
   - BusyBox/rootfs
   - optional toolchains
2. Run smoke workflows (QEMU and rv32emu end-to-end runs).
3. Perform environment checks and contract checks.

## Non-Scope

1. Emulator implementation details (`rv32emu/src`, `rv32emu/tests`).
2. Emulator-local post-processing tools.

Use `rv32emu/scripts/` for emulator-local tooling (e.g. trace analyzers).
