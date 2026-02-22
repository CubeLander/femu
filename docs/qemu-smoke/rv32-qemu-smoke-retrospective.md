# RV32 QEMU Smoke 复盘（2026-02-22）

## 目标
在 Docker 开发环境内完成以下闭环：
- 自动化拉取与构建 Linux / OpenSBI / BusyBox / RV32 toolchain。
- `qemu-system-riscv32` smoke test 在严格模式下通过（`ALLOW_INIT_PANIC=0`）。

## 最终结果
在 2026-02-22，`make smoke-qemu-strict` 已通过，日志见：
- `docs/logs/2026-02-22-qemu-smoke-success.log`

关键成功标志：
- OpenSBI banner 存在。
- Linux banner 存在。
- `Kernel command line` 存在。
- `Run /bin/sh as init process` 后进入 shell（`~ #`）。

## 技术要点
1. 架构一致性必须端到端对齐
- 内核必须是 RV32（`CONFIG_32BIT=y`）。
- 用户态 BusyBox 必须是 RV32 ELF。
- QEMU 使用 `qemu-system-riscv32`。

2. smoke 脚本应检查“能否启动”也检查“失败类型”
- `scripts/smoke-qemu.sh` 增加了对 `error -8` 的识别。
- 严格模式（`ALLOW_INIT_PANIC=0`）下，panic 和 `error -8` 都直接 fail。

3. rootfs 生成要避免执行目标架构二进制
- 不能在 x86_64 宿主机直接运行 RV32 BusyBox 来列 applet。
- 当前方案是读取 `busybox.links` 建链，而不是执行 `./busybox --list`。

4. RV32 ilp32d 用户态依赖内核 FPU 支持
- BusyBox 由 `riscv32-ilp32d` 工具链构建时，会出现 F/D 指令（例如 `fsd`）。
- 内核若 `CONFIG_FPU=n`，`/bin/sh` 会在 init 阶段触发 illegal instruction。

## 排障过程（问题 -> 证据 -> 修复）

### 问题 1：rootfs 构建时报 `Exec format error`
证据：
- `./scripts/build-rootfs.sh: line 30: ./busybox: cannot execute binary file: Exec format error`

原因：
- 旧流程在宿主机执行目标架构 BusyBox（RV32）来枚举 applet。

修复：
- 修改 `scripts/build-rootfs.sh`，改为：
  - 直接复制 `busybox` 到 `rootfs/bin/busybox`。
  - 读取 `out/busybox/busybox.links` 建立软链接。
- 参考：`scripts/build-rootfs.sh:23`。

### 问题 2：QEMU 进入 init 后报 `error -8`
证据：
- `Failed to execute /bin/sh (error -8)`
- `No working init found`

原因：
- rootfs 中 BusyBox 一度被重新构建成 RV64（和 RV32 内核不匹配）。

修复：
- BusyBox 构建脚本增加 RV32 优先探测与 `REQUIRE_RV32=1` 约束。
- `build-rootfs` 不再调用会触发重编译的 `make install` 路径。

### 问题 3：已是 RV32 仍在 init 触发 illegal instruction
证据：
- `sh[1]: unhandled signal 4 code 0x1 ...`
- `Kernel panic - not syncing: Attempted to kill init!`
- 反汇编崩溃点含 `fsd`（浮点存储指令）。

原因：
- BusyBox 为 ilp32d（硬浮点 ABI），而内核配置曾为 `CONFIG_FPU=n`。

修复：
- `scripts/config/linux-rv32-minimal.config` 设置 `CONFIG_FPU=y`。
- `scripts/build-linux.sh` 改为每次构建都 merge 配置片段，并增加 `CONFIG_FPU=y` 断言。
- 参考：
  - `scripts/config/linux-rv32-minimal.config:8`
  - `scripts/build-linux.sh:62`
  - `scripts/build-linux.sh:75`

## 过程沉淀（自动化增强）
- 新增 RV32 预编译工具链安装脚本：`scripts/install-rv32-toolchain.sh`。
- Makefile 增加目标：
  - `install-rv32-toolchain`
  - `build-busybox-rv32`
  - `smoke-qemu-strict`
- 环境检查新增 RV32 userspace compiler 探测：`scripts/check-env.sh`。

## 建议的稳定执行顺序
```bash
make check-env
make install-rv32-toolchain
make build-busybox-rv32
make build-linux
make build-opensbi
make build-rootfs
make smoke-qemu-strict
```

## 日志索引
- 成功日志：`docs/logs/2026-02-22-qemu-smoke-success.log`
- 手工启动日志：`docs/logs/2026-02-22-smoke-manual-opensbi.log`
- 故障签名摘要：`docs/logs/2026-02-22-qemu-smoke-failure-signatures.md`
