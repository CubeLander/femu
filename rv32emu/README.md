# rv32emu (clean-room scaffold)

This directory is the clean-room start point for a new RV32 Linux emulator,
independent from legacy course code paths.

Current scope in this repo:

1. Keep module boundaries explicit (`cpu/mmu/platform/sbi/loader`).
2. Move real implementation into a dedicated new repository after migration.
3. Preserve this folder as a takeaway seed for the new project.

See `issues/0001-comprehensive-report.md` and `docs/migration/` for migration details.
