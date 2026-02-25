# 自研 Emulator 复刻 OpenSBI -> Linux -> rootfs 总实施计划

## 1. 目标与边界
目标是让 `rv32emu` 在自研平台上稳定跑通：
- OpenSBI 启动并打印 banner。
- Linux 内核启动并完成 init handoff。
- initramfs(rootfs) 中 `/bin/sh` 可进入交互（smoke strict pass）。

当前优先边界：
- 优先 RV32 单核（与现有 `CONFIG_SMP=n` 保持一致）。
- 优先 initramfs 启动路径，不把 virtio/block/net 作为首阶段前置条件。

## 2. 成功标准（DoD）
1. 功能标准
- `opensbi -> linux -> /bin/sh` 全链路可重复通过。
- 与 `qemu-system-riscv32` 关键启动日志对齐（banner/cmdline/init handoff）。

2. 自动化标准
- 有 `smoke-emulator` 脚本，支持 strict 模式。
- strict 模式下，出现 panic、`error -8`、illegal instruction 直接失败。

3. 工程标准
- 文档、日志、构建脚本可在容器里一键复现。

## 3. 总任务分解（按阶段）

### 阶段 A：启动契约冻结（Boot Contract）
要做的事：
1. 固化内存映射与加载地址（DRAM/UART/CLINT/PLIC、kernel/dtb/initrd）。
2. 固化启动参数（`console=ttyS0 earlycon=sbi rdinit=/init`）。
3. 定义镜像加载顺序、入口点、寄存器约定（`a0=hartid`，`a1=dtb`）。

验收：
- 有一份明确的 Boot Contract 文档（可放在本文件附录或 `docs/legacy/boot-contract.md`）。

当前产出（2026-02-22）：
1. Boot Contract 文档：`docs/legacy/boot-contract.md`
2. 自动校验脚本：`scripts/check-boot-contract.sh`
3. 统一校验入口：`make check-boot-contract`

### 阶段 B：平台与外设最小可用
要做的事：
1. DRAM/ROM 与 MMIO 框架完成。
2. UART16550 完成（至少满足 console 输出与基本输入）。
3. ACLINT/CLINT 计时器与软件中断完成。
4. PLIC 最小实现完成（中断 claim/complete 路径可用）。

验收：
- OpenSBI 在 emulator 控制台稳定输出。

当前产出（2026-02-22）：
1. 最小平台骨架实现：`src/platform/rv32emu_platform.c` + `src/memory/rv32emu_memory_mmio.c`
2. 覆盖 DRAM + UART16550 + CLINT + PLIC(minimal) 的物理访存路径。
3. 单元自测：`tests/test_platform.c`
4. 构建/测试入口：`make rv32emu-test`

### 阶段 C：CPU/CSR/Trap 主链路
要做的事：
1. RV32 指令执行主路径（先保守正确性，后优化性能）。
2. M/S 模式切换、异常委托、`ecall/mret/sret`。
3. 中断仲裁与进入/返回流程。
4. 原子与同步语义（LR/SC、AMO）满足 Linux 早期需求。

验收：
- OpenSBI 能把控制权转交给 Linux，Linux banner 可见。

当前产出（2026-02-22）：
1. 初始 RV32 指令执行循环：`src/cpu/rv32emu_cpu_exec.c`
2. 已支持基础指令子集：`lui/auipc/jal/jalr/branch/op-imm/op/load/store/system(ecall/ebreak)`。
3. 已支持 `SYSTEM` 关键子集：`csrrw/csrrs/csrrc/csrrwi/csrrsi/csrrci/mret/sret/wfi`。
4. trap/中断基础路径已可触发：`illegal/misaligned/ecall/ebreak`，并支持跳转到 `mtvec/stvec`。
5. 已实现 `sstatus/sie/sip` 到 `mstatus/mie/mip` 的别名映射，以及 pending interrupt 自动投递。
6. 指令执行单测：
   - `tests/test_run.c`（算术 + trap 停机）
   - `tests/test_system.c`（CSR + mret/sret + trap-vector 往返 + interrupt trap + delegation + vectored）

### 阶段 D：Sv32 MMU 与内存权限模型
要做的事：
1. Sv32 页表遍历与权限检查。
2. `satp`/`sfence.vma` 语义。
3. A/D bit 行为与页异常处理一致性。

验收：
- Linux 可进入 init 阶段，不在早期内存管理阶段崩溃。

当前产出（2026-02-22）：
1. 已实现 Sv32 两级页表遍历与 leaf/superpage 检查：`src/cpu/rv32emu_virt_trap.c`
2. 已实现权限模型：
   - `U/S` 页访问检查
   - `SUM/MXR` 行为
3. 已实现 A/D bit 自动置位并回写 PTE（load 置 A，store 置 A+D）。
4. `satp` 语义已接入：
   - `M` 模式直通（不翻译）
   - `S/U + MODE=Sv32` 启用地址翻译
5. `sfence.vma` 最小语义已接入（S/M 可执行 no-op，U 触发 illegal）：`src/cpu/rv32emu_cpu_exec.c`
6. 新增测试覆盖：
   - `tests/test_system.c`：Sv32 翻译、权限 fault、A/D、`sfence.vma`
   - `tests/test_platform.c`：更新 satp 在 M 模式下的直通断言

### 阶段 E：OpenSBI 对接与 DTB
要做的事：
1. OpenSBI 固件加载与跳转流程稳定。
2. DTB 策略：先静态可用，再动态生成。
3. SBI 调用最小集闭环（timer/ipi/rfence/base，按 OpenSBI+Linux 需要逐步补齐）。

验收：
- `OpenSBI banner + Linux cmdline` 关键标记稳定出现。

当前产出（2026-02-22）：
1. SBI shim 最小闭环：`src/platform/rv32emu_stubs.c`
   - 已支持 legacy `set_timer/console/shutdown`。
   - 已支持 v0.2 `BASE/TIME/IPI/RFENCE/HSM/SRST` 的最小返回语义（单核 no-op/受限实现）。
2. ecall 执行路径打通：`src/cpu/rv32emu_cpu_exec.c`
   - 在 `enable_sbi_shim=true` 下，`ecall` 被 SBI 处理后可正常退休并前进到下一条指令。
3. 镜像加载器最小可用：`src/platform/rv32emu_stubs.c`
   - `rv32emu_load_raw`
   - `rv32emu_load_elf32`
   - `rv32emu_load_image_auto`（ELF32 自动识别）
4. 新增覆盖测试：
   - `tests/test_system.c`：SBI handle + ecall 前进路径
   - `tests/test_loader.c`：raw/ELF32/auto loader

### 阶段 F：rootfs 集成与 smoke 自动化
要做的事：
1. 复用当前 rootfs 产物链路（busybox links + initramfs）。
2. 新增 `smoke-emulator.sh`，复用 qemu smoke 的 marker/失败判定。
3. 接 CI（可先 nightly/手动触发）。

验收：
- strict smoke pass：进入 `/bin/sh` 且无 panic/error-8/illegal instruction。

当前产出（2026-02-22）：
1. emulator 运行主程序：`tools/rv32emu_main.c`
   - 可加载 OpenSBI/Linux/DTB/initramfs 并设置 RV32 启动寄存器。
   - 支持 `FW_DYNAMIC` 信息结构注入（`a2`）。
2. 第一版端到端脚本：`scripts/smoke-emulator.sh`
   - 复用 qemu smoke 的关键 marker 与 panic/error 检测。
   - 自动 patch `/chosen`（`bootargs` + `linux,initrd-start/end`）。
3. 顶层入口：
   - `make rv32emu-bin`
   - `make smoke-emulator`
   - `make smoke-emulator-strict`
4. 第二轮 smoke 结果（已推进到 strict pass）：
   - 关键 marker：`OpenSBI banner`、`Linux version`、`Kernel command line`、`Run /init as init process` 均可命中。
   - 参考日志：
     - `docs/logs/2026-02-22-emulator-smoke-round2.log`
     - `docs/logs/2026-02-22-emulator-smoke-strict-pass.log`
   - 通过修复：
     - `MSTATUS.MPRV` 数据访问翻译语义（OpenSBI/Linux 交互路径）
     - `virtio-mmio` 最小空设备桩（避免 probe access fault）
     - smoke DTB patch 数值写入（`fdtput -t x` 十六进制格式）与 `memory` 尺寸同步
     - 移除暂未实现外设节点（rtc/fw-cfg/flash/pci）
     - 增补 `scounteren/mcounteren` CSR
     - 增补 F/D 最小执行路径：
       - `FLW/FSW/FLD/FSD`
       - `C` 扩展浮点访存（`c.fld/c.fsd/c.fldsp/c.fsdsp/c.flw/c.fsw/c.flwsp/c.fswsp`）
       - 基础 OP-FP 搬运与符号注入（`fmv.x.w/fmv.w.x/fsgnj.s/fsgnj.d`）
     - `misa` 扩展位更新到 `rv32imafdcsu`
   - 当前状态：
     - `make smoke-emulator` 通过
     - `make smoke-emulator-strict` 通过（进入 `/bin/sh`，无 panic）

## 4. 阶段 G：SMP 基础框架（进行中）
已完成（2026-02-22）：
1. 多 hart 数据结构骨架：
   - `rv32emu_machine_t` 新增 `harts[]/hart_count/active_hart`。
   - `rv32emu_options_t` 新增 `hart_count`，默认 `1`，上限 `4`。
2. 运行时调度骨架：
   - `rv32emu_run()` 改为按 hart 轮转（单线程）调度。
   - 维持单核默认行为不变，现有 smoke 回归通过。
3. SBI HSM 最小语义：
   - `hart_start/hart_stop/hart_status` 打通基础状态流。
   - 新增单测覆盖 `HSM start/status`。
4. 工具链入口：
   - `rv32emu_main` 新增 `--hart-count` 参数。
5. CLINT per-hart：
   - `msip[]/mtimecmp[]` 按 hart 建模，`step_timer` 按 hart 同步 `TIME/MTIP(STIP)`。
6. PLIC per-context：
   - 以 `2 * hart_count` 建模 `enable/claim/complete`，支持多 context 寄存器地址解码。
7. 回归测试补强：
   - `test_platform` 新增多核 CLINT/PLIC 场景（hart1 的 `MSIP/MTIMECMP/MEIP`）。
8. 首轮 `CONFIG_SMP=y` bring-up 链路已打通：
   - 新增内核配置片段：`scripts/config/linux-rv32-smp.config`
   - 新增构建目标：`make build-linux-smp`
   - 新增 smoke 目标：`make smoke-emulator-smp`
   - `smoke-emulator-smp` 调整为两阶段验收：
     - 阶段1：`hart_count=1` + SMP kernel，校验 `Linux banner/init handoff`。
     - 阶段2：`hart_count=2`，校验次核拉起（`hart1 state: running=1`）。
   - 参考日志：`docs/logs/2026-02-22-emulator-smp-hart1-pass.log`
9. LR/SC 多核语义补强：
   - 在 `virt_write` 成功后按地址区间失效所有 hart 的 LR reservation，避免跨核写后 `sc.w` 误成功。
   - 新增单测：`test_multihart_lr_sc_invalidation`。
10. 多核调度开销优化：
   - `rv32emu_run()` 改为每 hart 小时间片（64 指令）轮转，减少每条指令级别的 hart 状态交换开销。
11. OpenSBI 多核启动模型修正：
   - OpenSBI 路径下，所有 hart 从固件入口启动；SBI shim 路径下保持仅 hart0 启动。
   - 新增严格双核验收目标：`make smoke-emulator-smp-linux`（stage2 验证 `Linux banner/cmdline/init handoff + hart1 running`）。

当前限制：
1. IPI/RFENCE 仍为最小实现，不足以支撑完整 Linux SMP。
2. 当前调度为单线程轮转，不包含 host 并行。
3. PLIC 仍是最小子集（仅低 32 路中断、priority/threshold 语义未完整建模）。
4. 默认 `make smoke-emulator-smp` 的 stage2 仍是快速次核拉起检查（120M 指令）；完整双核 Linux marker 验证由 `make smoke-emulator-smp-linux` 承担。

下一步（阶段 G-2 / G-3）：
1. 在现有 `MSIP` 唤醒路径基础上，补齐 IPI/RFENCE 的 hart-mask 语义与状态一致性检查。
2. 评估将 `smoke-emulator-smp-linux` 的双核严格 marker 检查提升为默认 CI 档位（或 nightly）。
3. 扩展 PLIC 语义并引入更多中断源做回归。

## 5. 推荐里程碑
1. M1：OpenSBI banner 出现。
2. M2：Linux banner + cmdline 出现。
3. M3：`Run /init as init process`。
4. M4：strict smoke pass（进入 `~ #`）。

## 6. 多核问题：是否现在就做？
结论（建议）：
- 现在先不做多核作为前置。
- 先把单核 strict smoke 跑稳，再进入多核阶段。

理由：
1. 当前内核配置和 bring-up 目标是单核优先（`CONFIG_SMP=n`）。
2. 多核会显著放大调试复杂度（中断、同步、时序、竞态），会拖慢首个端到端闭环。
3. 单核跑通后，多核问题可拆分成独立里程碑，风险更可控。

## 7. 如果做多核，需要额外关心什么
1. 每个 hart 的独立状态
- 独立 PC/寄存器/CSR/priv/trap 上下文。
- 每个 hart 的中断挂起与使能状态。

2. 定时器与 IPI 的“每核语义”
- 每核 `mtimecmp` 行为。
- MSIP/SSIP 跨核触发与清除。

3. PLIC 多上下文路由
- 不同 hart + 不同 privilege context 的 claim/complete 路径。
- 外设中断目标核路由策略。

4. 内存模型与原子一致性
- LR/SC reservation 在多核下的失效规则。
- AMO 的全局原子性。
- 共享内存并发访问的可见性与顺序语义。

5. TLB 与远程失效
- `sfence.vma` 本地与远程语义。
- 通过 SBI RFENCE 做跨核 shootdown 的闭环。

6. OpenSBI HSM 与次核启动流程
- `sbi_hart_start/stop/status` 行为。
- 次核上电后入口、栈、上下文初始化。

7. DTB 的多核描述
- `cpus` 节点、`interrupt-controller`、IPI/timer 绑定必须一致。

8. 调度与可复现性
- 多核仿真步进策略（轮询/时间片/事件驱动）要可复现。
- 必须有 per-hart trace 与时序日志，不然很难定位竞态 bug。

## 8. 多核实施建议（在单核稳定后）
1. SMP-1：2 核启动，但次核先 park（验证 HSM 基础链路）。
2. SMP-2：2 核都进入 Linux，跑最小 SMP 启动验证。
3. SMP-3：压测锁/中断/定时器，补齐稳定性与回归测试。

## 9. 本计划对应的仓库落点
- emulator scaffold：`rv32emu/`
- QEMU 基线与复盘：`docs/qemu-smoke/rv32-qemu-smoke-retrospective.md`
- 参考日志：`docs/logs/2026-02-22-qemu-smoke-success.log`
