# QEMU Smoke 故障签名摘要（2026-02-22）

用途：记录本次 RV32 bring-up 过程中出现过的典型失败模式，用于后续快速定位。

## 1) rootfs 构建阶段：宿主执行目标架构 BusyBox 失败

```text
./scripts/build-rootfs.sh: line 30: ./busybox: cannot execute binary file: Exec format error
make: *** [Makefile:49: build-rootfs] Error 126
```

判定：构建脚本在 x86_64 宿主执行了 RV32 ELF。

## 2) init 交接阶段：用户态架构不匹配（error -8）

```text
Run /bin/sh as init process
Failed to execute /bin/sh (error -8)
Starting init: /sbin/init exists but couldn't execute it (error -8)
Kernel panic - not syncing: No working init found.
```

判定：内核是 RV32，但 rootfs 中 init/BusyBox 不是可执行的 RV32 用户态二进制（常见为 RV64）。

## 3) init 运行阶段：illegal instruction（FPU 配置不匹配）

```text
Run /bin/sh as init process
sh[1]: unhandled signal 4 code 0x1 at 0x00114648 in busybox[10000+23a000]
status: 00000020 badaddr: 02853c27 cause: 00000002
Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000004
```

附加证据（崩溃点反汇编）：

```text
114648: 02853c27    fsd fs0,56(a0)
```

判定：BusyBox 由 `ilp32d` 工具链构建，用户态使用了 F/D 指令；内核若未启用 FPU 支持会在 init 阶段崩溃。

## 4) 成功签名（最终状态）

```text
[OK] marker found: OpenSBI banner
[OK] marker found: Linux banner
[OK] marker found: kernel cmdline
[OK] marker found: init handoff
[OK] qemu smoke test passed
Run /bin/sh as init process
~ #
```

参考完整日志：
- `docs/logs/2026-02-22-qemu-smoke-success.log`
