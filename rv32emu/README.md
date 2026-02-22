# rv32emu (clean-room scaffold)

This directory is the clean-room start point for a new RV32 Linux emulator,
independent from legacy course code paths.

Current scope in this repo:

1. Keep module boundaries explicit (`cpu/mmu/platform/sbi/loader`).
2. Move real implementation into a dedicated new repository after migration.
3. Preserve this folder as a takeaway seed for the new project.

Phase-B/C/E status in this repository:

1. Minimal platform model is split into module files under `src/`.
2. Implemented memory + MMIO skeleton: DRAM, UART16550, CLINT, PLIC(minimal).
3. Implemented CPU trap/csr execution core with interrupt/delegation tests.
4. Implemented minimal SBI shim and image loader (`raw`/`ELF32`/`auto`).
5. Basic unit tests are available:
   - `tests/test_platform.c`
   - `tests/test_run.c`
   - `tests/test_system.c`
   - `tests/test_loader.c`

Build/test:

```bash
make -C rv32emu test
```

Build emulator runner:

```bash
make -C rv32emu rv32emu
```

See `issues/0001-comprehensive-report.md` and `docs/migration/` for migration details.
