# Linux Tracing MVP

This chapter documents the current Linux tracing MVP flow for `rv32emu`.

## 1) Capture Trace

Use `--trace` to enable core event tracing (`syscall + trap/interrupt`).

```bash
./build/rv32emu \
  --opensbi out/opensbi/platform/generic/firmware/fw_dynamic.bin \
  --kernel out/linux/arch/riscv/boot/Image \
  --dtb out/smoke/virt-rv32-smoke.dtb \
  --initrd out/rootfs/initramfs.cpio.gz \
  --max-instr 1200000000 \
  --trace \
  --trace-file out/trace/linux-boot.trace.log
```

`--trace-file` now auto-creates missing parent directories (for example `out/trace/`).

Optional MMIO events:

```bash
./build/rv32emu ... --trace --trace-mmio --trace-file out/trace/linux-boot-mmio.trace.log
```

## 2) Protect Against Log Explosion

Tracing can produce very large logs in fault loops or long runs.  
Set `RV32EMU_TRACE_MAX_EVENTS` to cap emitted events.

```bash
export RV32EMU_TRACE_MAX_EVENTS=2000000
```

When the cap is hit, trace output stops and a one-line marker is emitted.

## 3) Symbolize and Summarize

Convert raw trace addresses to Linux symbols with `System.map`:

```bash
python3 scripts/trace_linux_path.py \
  --trace out/trace/linux-boot.trace.log \
  --system-map out/linux/System.map \
  --symbol-bias 0x40000000 \
  --top 30 \
  --timeline 80 \
  --phase-window 50000 \
  --phase-max 16 \
  --json-out out/trace/linux-boot.summary.json
```

For RV32 Linux early boot, trace PCs are often in `0x80000000...` while `System.map` is linked at
`0xc0000000...`; use `--symbol-bias 0x40000000` to align lookups.

Subsystem focus (scheduler / network):

```bash
python3 scripts/trace_linux_path.py \
  --trace out/trace/linux-boot.trace.log \
  --system-map out/linux/System.map \
  --symbol-bias 0x40000000 \
  --priv S \
  --focus-profile scheduler \
  --json-out out/trace/linux-boot.scheduler.summary.json

python3 scripts/trace_linux_path.py \
  --trace out/trace/linux-boot.trace.log \
  --system-map out/linux/System.map \
  --symbol-bias 0x40000000 \
  --priv S \
  --focus-profile network \
  --json-out out/trace/linux-boot.network.summary.json
```

Custom focus keywords can be stacked:

```bash
python3 scripts/trace_linux_path.py \
  --trace out/trace/linux-boot.trace.log \
  --system-map out/linux/System.map \
  --symbol-bias 0x40000000 \
  --focus-symbol do_sys_open \
  --focus-symbol vfs_read
```

Per-hart summary:

```bash
python3 scripts/trace_linux_path.py \
  --trace out/trace/linux-boot.trace.log \
  --system-map out/linux/System.map \
  --hart 0
```

## 4) Output Interpretation

The summarizer prints:

1. Event distribution (`syscall`, `exception`, `interrupt`, `mmio_*`).
2. Trap cause distribution and delegation.
3. Syscall/SBI ID hotspots (`a7/a6`).
4. PC hotspots and trap path transitions (`from_pc -> target_pc`, symbolized).
5. A timeline prefix to inspect early boot path.
6. Optional phase report (event-window chunks) to quickly spot `trap_storm` / `syscall_hotpath`.

This is the MVP for “Linux call-path visibility”; next iterations can add:

1. Cross-event causal stitching (syscall to trap to return).
2. Symbolized flamegraph export.
3. Guest kernel/user symbol separation with richer metadata.
