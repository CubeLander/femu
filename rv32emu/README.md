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

Interactive UART console (stdin -> UART RX):

```bash
./rv32emu/build/rv32emu \
  --opensbi out/opensbi/platform/generic/firmware/fw_dynamic.bin \
  --kernel out/linux/arch/riscv/boot/Image \
  --dtb out/smoke/virt-rv32-smoke.dtb \
  --initrd out/rootfs/initramfs.cpio.gz \
  --hart-count 1 \
  --interactive \
  --max-instr 1200000000
```

Trace Linux boot path:

```bash
./rv32emu/build/rv32emu \
  --opensbi out/opensbi/platform/generic/firmware/fw_dynamic.bin \
  --kernel out/linux/arch/riscv/boot/Image \
  --dtb out/smoke/virt-rv32-smoke.dtb \
  --initrd out/rootfs/initramfs.cpio.gz \
  --max-instr 1200000000 \
  --trace \
  --trace-file out/trace/linux-boot.trace.log
```

Symbolize trace and print path summary:

```bash
python3 scripts/trace_linux_path.py \
  --trace out/trace/linux-boot.trace.log \
  --system-map out/linux/System.map \
  --symbol-bias 0x40000000 \
  --json-out out/trace/linux-boot.summary.json
```

See `issues/0001-comprehensive-report.md` and `docs/migration/` for migration details.
