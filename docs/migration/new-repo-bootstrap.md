# New Repo Bootstrap Guide

## 1. Create new repository

```bash
mkdir -p ~/work/rv32linux-emu
cd ~/work/rv32linux-emu
git init
```

## 2. Copy takeaway package from current repo

From current repo root (`ics2019`):

```bash
./scripts/takeaway.sh
```

Then unpack into the new repo:

```bash
tar -xzf /path/to/ics2019/out/takeaway/ics2019-takeaway-*.tar.gz -C ~/work/rv32linux-emu
```

## 3. Start dev container

If you are on Windows/WSL2, complete this first:

```text
docs/migration/wsl2-docker-setup.md
docs/migration/network-proxy.md
```

```bash
./scripts/dev-shell.sh
```

## 4. Build baseline artifacts

Inside container:

```bash
./scripts/fetch-sources.sh
./scripts/install-rv32-toolchain.sh
./scripts/build-busybox.sh
./scripts/build-linux.sh
./scripts/build-opensbi.sh
./scripts/build-rootfs.sh
./scripts/smoke-qemu.sh
```

If cross compiler is missing:

```bash
./scripts/build-toolchain.sh
```

For strict rv32 smoke:

```bash
RV32_CROSS_COMPILE="$(pwd)/opt/toolchains/riscv32-ilp32d--glibc--stable-2025.08-1/bin/riscv32-linux-" REQUIRE_RV32=1 ./scripts/build-busybox.sh
ALLOW_INIT_PANIC=0 ./scripts/smoke-qemu.sh
```

## 5. Add emulator clean-room project

Recommended structure:

```text
emulator/
  include/
  src/
  tests/
```

Commit strategy:

1. infra only (container + scripts + docs)
2. platform only (RAM/UART/CLINT/PLIC skeleton)
3. CPU interpreter incrementally
4. Linux boot milestones

## 6. Suggested initial milestone definition

Milestone-0 (infra):

1. container enters successfully
2. Linux/OpenSBI/rootfs artifacts can be rebuilt via scripts
3. all outputs under `out/`

Milestone-1 (emulator minimum):

1. serial output works
2. simple rv32 bare-metal test passes
3. instruction trace + trap debug output available
