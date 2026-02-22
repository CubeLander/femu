# Source Bootstrap and Build Automation

This repository supports automated source sync and baseline artifact build for:

1. Linux
2. OpenSBI
3. BusyBox
4. Optional RISC-V GNU toolchain

## 1. Source sync

From repo root:

```bash
./scripts/fetch-sources.sh
```

Default pinned refs:

1. Linux: `v6.6`
2. OpenSBI: `v1.5.1`
3. BusyBox: `1_36_1`

Optional toolchain source checkout:

```bash
FETCH_TOOLCHAIN=1 ./scripts/fetch-sources.sh
```

Useful overrides:

```bash
LINUX_REF=v6.12 OPENSBI_REF=v1.6 BUSYBOX_REF=1_37_0 ./scripts/fetch-sources.sh
UPDATE_EXISTING=0 ./scripts/fetch-sources.sh
SHALLOW_CLONE=0 ./scripts/fetch-sources.sh
```

## 2. Toolchain setup (if needed)

Auto mode:

```bash
./scripts/build-toolchain.sh
```

Behavior:

1. If system toolchain already exists, do nothing.
2. If missing and `apt-get` is available, install distro packages.
3. Otherwise, build from `riscv-gnu-toolchain` source.

Force source mode:

```bash
TOOLCHAIN_MODE=source ./scripts/build-toolchain.sh
```

## 3. Build artifacts

```bash
./scripts/build-busybox.sh
./scripts/build-linux.sh
./scripts/build-opensbi.sh
./scripts/build-rootfs.sh
./scripts/smoke-qemu.sh
```

Strict mode:

```bash
ALLOW_INIT_PANIC=0 ./scripts/smoke-qemu.sh
```

Outputs:

1. BusyBox: `out/busybox/busybox`
2. Linux: `out/linux/arch/riscv/boot/Image`
3. OpenSBI: `out/opensbi/platform/generic/firmware/fw_dynamic.bin`
4. Rootfs: `out/rootfs/initramfs.cpio.gz`
5. Smoke log: `out/smoke/qemu-smoke.log`

## 4. Makefile shortcuts

```bash
make fetch-sources
make build-toolchain
make build-busybox
make build-all
make bootstrap
make smoke-qemu
```

`make bootstrap` runs toolchain check/setup, source sync, and baseline builds end-to-end.
