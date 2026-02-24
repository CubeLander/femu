# Emulator 文档索引

- 总实施计划：`docs/emulator/opensbi-linux-rootfs-implementation-plan.md`
- 双核 Linux 调通技术总结：`docs/emulator/smp-linux-bringup-technical-summary.md`
- SMP threaded 内存语义清单：`docs/emulator/smp-threaded-memory-semantics-checklist.md`
- 块级 JIT 启动计划：`docs/emulator/block-jit-kickoff-plan.md`
- Phase-1 Boot Contract：`docs/emulator/boot-contract.md`
- QEMU 基线复盘：`docs/qemu-smoke/rv32-qemu-smoke-retrospective.md`
- 成功日志样例：`docs/logs/2026-02-22-qemu-smoke-success.log`
- Emulator 第一轮日志：`docs/logs/2026-02-22-emulator-smoke-round1.log`
- Emulator 第二轮日志：`docs/logs/2026-02-22-emulator-smoke-round2.log`
- Emulator strict pass 日志：`docs/logs/2026-02-22-emulator-smoke-strict-pass.log`
- Emulator SMP smoke 日志：`docs/logs/2026-02-22-emulator-smp-smoke.log`
- Emulator SMP 次核拉起日志：`docs/logs/2026-02-22-emulator-smp-hart1-pass.log`

Quick checks:

- `make check-boot-contract`
- `make rv32emu-test`
- `make rv32emu-bin`
- `make rv32emu-bin-perf`
- `make smoke-emulator`
- `make smoke-emulator-jit`
- `make smoke-emulator-perf`
- `make smoke-emulator-jit-perf`
- `make smoke-emulator-tb-perf`
- `make smoke-emulator-strict`
- `make build-linux-smp`
- `make smoke-emulator-smp`
- `make smoke-emulator-smp-linux`
- `make smoke-emulator-smp-threaded`
- `make smoke-emulator-smp-linux-threaded`

`make smoke-emulator-smp` 会串行执行两段验收：
1. `hart_count=1` 下验证 `Linux banner` + `init handoff`。
2. `hart_count=2` 下验证次核拉起（`hart1 state: running=1`）。

`make smoke-emulator-smp-linux` 保持阶段 1 不变，并把阶段 2 升级为严格双核 Linux marker 验收：
`Linux banner` + `kernel cmdline` + `init handoff` + `hart1 running`。

`make smoke-emulator-smp-linux-threaded` 在相同严格条件下启用
`RV32EMU_EXPERIMENTAL_HART_THREADS=1`，用于回归“每个 hart 一个 host 线程”实验路径。
