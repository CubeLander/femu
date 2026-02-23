# Emulator 文档索引

- 总实施计划：`docs/emulator/opensbi-linux-rootfs-implementation-plan.md`
- 双核 Linux 调通技术总结：`docs/emulator/smp-linux-bringup-technical-summary.md`
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
- `make smoke-emulator`
- `make smoke-emulator-strict`
- `make build-linux-smp`
- `make smoke-emulator-smp`
- `make smoke-emulator-smp-linux`

`make smoke-emulator-smp` 会串行执行两段验收：
1. `hart_count=1` 下验证 `Linux banner` + `init handoff`。
2. `hart_count=2` 下验证次核拉起（`hart1 state: running=1`）。

`make smoke-emulator-smp-linux` 保持阶段 1 不变，并把阶段 2 升级为严格双核 Linux marker 验收：
`Linux banner` + `kernel cmdline` + `init handoff` + `hart1 running`。
