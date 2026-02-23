# RV32EMU 双核 Linux 启动调通技术总结（2026-02-23）

## 1. 背景与目标

目标是让自研 `rv32emu` 在 **双核（`hart_count=2`）** 条件下，稳定完成以下链路：

1. OpenSBI 正常启动并交接。
2. Linux（`CONFIG_SMP=y`）出现启动关键 marker。
3. 能进入 initramfs 并看到 `Run /init as init process`。
4. 次核处于运行态（`hart1 state: running=1`）。

最终可复现验收命令：

```bash
make smoke-emulator-smp-linux
```

该目标等价于：

1. 阶段1（`hart_count=1`）：严格 Linux marker 校验。
2. 阶段2（`hart_count=2`）：严格 Linux marker + `hart1 running` 校验。

---

## 2. 结果概览（当前状态）

双核严格 smoke 已通过，关键日志如下：

1. `out/smoke/emulator-smoke-smp-stage2.log`
   - `Linux version` 出现
   - `Run /init as init process` 出现
   - 末尾 `hart1 state: running=1`
2. `out/smoke/emulator-smoke-smp-linux-1200m-allboot-t120.log`
   - 同样命中 Linux/init/hart1 marker

代码侧对应落点：

1. OpenSBI 多核启动模型修正：`rv32emu/tools/rv32emu_main.c`
2. LR/SC 多核语义修正：`rv32emu/src/rv32emu_virt_trap.c`
3. 多核调度切片优化：`rv32emu/src/rv32emu_cpu_exec.c`
4. 双核 LR/SC 回归测试：`rv32emu/tests/test_run.c`

---

## 3. 启动链路是怎么打通的

### 3.1 引导入口（OpenSBI 路径）

在 `rv32emu_main` 中，为每个 hart 设置：

1. `pc = opensbi_entry`
2. `priv = M`
3. `a0 = hartid`, `a1 = dtb`, `a2 = fw_dynamic_info`

关键点在 `running` 的初始化策略：

1. OpenSBI 路径（默认）：**所有 hart 都置为 running**，让每个 hart 从 OpenSBI 入口进入固件。
2. `--sbi-shim` 路径：仅 hart0 running（Linux 用 `hart_start` 拉起次核）。

实现位置：`rv32emu/tools/rv32emu_main.c`。

### 3.2 次核唤醒（CLINT MSIP）

CLINT `MSIP` 写路径中，如果目标 hart 尚未运行，会把它置为 `running=true`，并设置对应 `MIP.MSIP`。  
实现位置：`rv32emu/src/rv32emu_memory_mmio.c`。

### 3.3 多核执行调度

`rv32emu_run()` 使用单线程轮转调度；每个 hart 连续执行一个指令切片（当前 `64` 条），再切换到下一 hart。  
实现位置：`rv32emu/src/rv32emu_cpu_exec.c`。

---

## 4. 调通过程中克服的核心困难

### 4.1 困难一：看似“双核成功”，实际是“单核运行”

早期有一组日志看起来已经双核成功（OpenSBI 打印 `Platform HART Count: 2`），并且 Linux 能到 `/init`。  
但深入看同一日志文件：

1. `out/smoke/emulator-smoke-smp-kernel-up-probe.log:4` 显示 `"[INFO] harts=1"`
2. 说明 emulator 实际只运行了 1 个 hart（OpenSBI 的 2-hart 信息来自 DTB/平台描述，不等于都在执行）

这一步纠正了一个重要误判：**必须以 emulator 自身 `harts=` 与末尾每个 hart state 为准**，不能只看 OpenSBI banner。

---

### 4.2 困难二：双核场景下 Linux marker 长时间缺失

典型失败日志：

1. `out/smoke/emulator-smoke-smp-linux-400m-after-slice.log`
2. `out/smoke/emulator-smoke-smp-800m-progress.log`

现象：

1. OpenSBI banner 正常
2. 两个 hart 都在跑（末尾 `hart1 state: running=1`）
3. 但在 4e8/8e8 指令窗口里没有 `Linux version` marker

最初难点在于可观测性：不知道是 Linux 没跑起来，还是跑起来但串口路径/时间窗口没覆盖到。

应对策略：

1. 建立两阶段 smoke（先保证 stage1 严格，stage2 先看次核拉起）
2. 做更长窗口的严格双核跑（`MAX_INSTR=1.2e9`）

结果：在更长窗口内验证到 Linux/init marker，说明问题主要是“模型偏差 + 验收窗口不足”的组合，而不是内核完全未启动。

---

### 4.3 困难三：OpenSBI 多核启动模型与真实固件预期不一致

根因点：

1. 旧逻辑在 OpenSBI 路径下只让 hart0 进入固件，次核长期处于“未运行”。
2. 真实 OpenSBI 多核流程中，所有 hart 都应进入固件并在 warmboot/park 路径等待调度或 IPI/HSM 事件。

修复：

1. OpenSBI 路径：全部 hart `running=true`，一起从 `opensbi_entry` 进入固件。
2. 仅在 `sbi_shim` 模式维持“只启动 hart0”。

修复位置：

1. `rv32emu/tools/rv32emu_main.c`

修复后直接效果：

1. `out/smoke/emulator-smoke-smp-linux-1200m-allboot.log` 与
   `out/smoke/emulator-smoke-smp-linux-1200m-allboot-t120.log` 均出现 Linux/init marker。

---

### 4.4 困难四：LR/SC 在多核下的一致性语义

如果 reservation 只在本核清除，不做跨核失效，`sc.w` 可能在错误条件下成功，破坏 Linux 自旋锁/原子原语语义。

修复策略：

1. 在 `virt_write` 成功提交后，扫描所有 hart reservation。
2. 只要写地址与 reservation 对应字有重叠，就失效该 hart 的 reservation。
3. 该逻辑统一覆盖普通 store、AMO、`sc.w` 的写回路径。

实现与注释位置：

1. `rv32emu/src/rv32emu_virt_trap.c`
2. `rv32emu/src/rv32emu_cpu_exec.c`（`lr.w/sc.w/amo` 语义注释）

回归测试：

1. `rv32emu/tests/test_run.c:test_multihart_lr_sc_invalidation`
2. 人工构造“hart0: lr -> (64 nop) -> sc；hart1: 中间插入 store”，验证 `sc.w` 必须失败（`rd=1`）。

---

### 4.5 困难五：多核轮转成本与行为可重复性

早期按“每条指令都切 hart”会带来较高切换成本，也让某些并发行为更难稳定复现。  
优化后改为每 hart `64` 指令切片。

实现：

1. `RV32EMU_HART_SLICE_INSTR=64`
2. `rv32emu_run()` 中按切片轮转

位置：

1. `rv32emu/src/rv32emu_cpu_exec.c`

---

## 5. 验收体系怎么设计，为什么这么设计

### 5.1 两阶段 smoke（默认）

脚本：`scripts/smoke-emulator-smp.sh`

1. 阶段1（`hart_count=1`）：
   - 严格 Linux marker（`Linux version`、`Kernel command line`、`Run /init`）
   - 用于保证基线稳定
2. 阶段2（`hart_count=2`）：
   - 默认快速检查 `hart1 running`
   - 用于快速发现“次核完全拉不起来”的回归

对应命令：

```bash
make smoke-emulator-smp
```

### 5.2 严格双核 smoke（新增）

为了把“真正双核 Linux 启动”固化为可重复门禁，新增目标：

```bash
make smoke-emulator-smp-linux
```

特性：

1. 阶段2 提升到 `1.2e9` 指令窗口
2. 阶段2 同时要求：
   - `Linux banner`
   - `kernel cmdline`
   - `init handoff`
   - `hart1 running`

---

## 6. 关键经验总结

1. **不要把 OpenSBI banner 当作“多核正在执行”的充分证据**。  
   要同时看 `"[INFO] harts="` 与每个 hart 的 stop state。

2. **多核 bring-up 必须把“功能正确性”和“验收窗口”同时建模**。  
   功能正确但窗口过短，结果也会表现为“marker 丢失”。

3. **LR/SC 语义是 Linux SMP 可用性的硬门槛**。  
   即使系统能“跑起来”，原子语义不对最终也会导致不稳定或隐性错误。

4. **先建立分层验收（stage1/stage2），再上严格门禁**，效率更高。  
   先保回归速度，再保严格正确性，二者不冲突。

---

## 7. 当前仍存在的限制与后续计划

1. IPI/RFENCE 目前仍是最小语义，尚不足以覆盖完整 Linux SMP 行为矩阵。
2. PLIC 仍是最小子集（中断路数与优先级/threshold 语义未完整）。
3. 日志中仍可见 soft lockup 警告，但在当前配置下不阻塞到 `/init`。

后续建议：

1. 将 `make smoke-emulator-smp-linux` 纳入 CI（至少 nightly）。
2. 优先补齐 IPI/RFENCE 的 hart-mask 与跨核一致性语义。
3. 增加更多并发原子/中断回归用例，覆盖 Linux 早期 SMP 常用路径。
