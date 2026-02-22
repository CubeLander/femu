# Boot Contract（Phase-1 冻结版）

## 1. 目的
定义自研 `rv32emu` 运行 `OpenSBI -> Linux -> initramfs` 的最小启动契约，确保镜像构建、加载地址、寄存器约定和校验规则一致。

## 2. 固定内存映射（RV32 virt-like）

| 区域 | 基地址 | 说明 |
|---|---:|---|
| DRAM | `0x80000000` | 主内存起始 |
| UART16550 | `0x10000000` | 串口控制台 |
| CLINT/ACLINT | `0x02000000` | 定时器与软件中断 |
| PLIC | `0x0c000000` | 外部中断控制 |

默认内存大小：`256 MiB`。

## 3. 镜像加载布局（默认）

| 镜像 | 路径 | 加载地址 |
|---|---|---:|
| OpenSBI FW_DYNAMIC | `out/opensbi/platform/generic/firmware/fw_dynamic.bin` | `0x80000000` |
| Linux Image | `out/linux/arch/riscv/boot/Image` | `0x80400000` |
| DTB | `out/virt-rv32.dtb` | `0x87f00000` |
| initramfs | `out/rootfs/initramfs.cpio.gz` | `0x88000000` |

约束：
- 所有镜像必须落在 DRAM 区间内。
- 所有镜像区域不得重叠。
- kernel 与 busybox 必须是 `ELF 32-bit`。

## 4. 入口寄存器约定
Linux 接管时：
- `a0 = hartid`（首阶段固定为 `0`）
- `a1 = dtb_pa`（DTB 物理地址）
- `pc = kernel_entry`

## 5. 启动参数约定
默认命令行：

```text
console=ttyS0 earlycon=sbi rdinit=/init
```

对应内核配置关键项：
- `CONFIG_32BIT=y`
- `CONFIG_FPU=y`（当前 userspace 为 `ilp32d`）
- `CONFIG_CMDLINE="console=ttyS0 earlycon=sbi rdinit=/init"`

## 6. 自动校验
使用以下命令验证契约：

```bash
make check-boot-contract
```

该检查会做：
1. 必需镜像存在性检查。
2. 若 DTB 缺失，自动尝试 `dump-virt-dtb.sh` 生成。
3. 镜像地址范围/重叠检查。
4. kernel/busybox 架构检查（ELF32）。
5. Linux 关键配置项检查（`CONFIG_32BIT`/`CONFIG_FPU`/`CONFIG_CMDLINE`）。

## 7. 变更控制
以下变更必须同步更新本文件和检查脚本：
- DRAM/MMIO 基地址变化。
- kernel/dtb/initramfs 加载地址变化。
- userspace ABI 变化（例如从 `ilp32d` 改为 `ilp32`）。
- Linux 启动命令行变化。
