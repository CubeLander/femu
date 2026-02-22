# Dependency Matrix (RV32 Linux Emulator Project)

| Layer | Input | Output | Recommended Provider | Notes |
|---|---|---|---|---|
| Container runtime | Docker/Podman | Reproducible build env | Host-installed only | Only host prerequisite |
| Cross toolchain | GCC/Binutils for RISC-V | `riscv64-linux-gnu-*` (or compatible) | `apt` in dev container | First choice for speed |
| Linux kernel | `linux/` source + config fragment | `out/linux/arch/riscv/boot/Image` | `scripts/fetch-sources.sh` + `scripts/build-linux.sh` | RV32 minimal fragment included |
| OpenSBI | `opensbi/` source | `out/opensbi/fw_dynamic.bin` | `scripts/fetch-sources.sh` + `scripts/build-opensbi.sh` | `PLATFORM=generic`, XLEN=32 |
| BusyBox/rootfs | `busybox` binary/source | `out/rootfs/initramfs.cpio.gz` | `scripts/fetch-sources.sh` + `scripts/build-busybox.sh` + `scripts/build-rootfs.sh` | Uses `fakeroot`, no host root required |
| Emulator | new clean-room implementation | emulator binary | new repo `emulator/` | Keep isolated from legacy NEMU |

## Host-level dependencies (minimum)

1. Docker or Podman
2. Git

Everything else should run in container.

## Windows/WSL2 onboarding

For Windows developers using WSL2, setup instructions are here:

- `docs/migration/wsl2-docker-setup.md`
- `docs/migration/network-proxy.md`

The key requirement is Docker Desktop with WSL integration enabled for your distro.
If you are behind GFW, see `docs/migration/network-proxy.md` and run with:

```bash
ENABLE_CLASH_PROXY=1 CLASH_PROXY_PORT=7890 ./scripts/dev-shell.sh
```

## Version pinning recommendation

Track versions in `docs/migration/versions.md` in new repo:

- Linux commit/tag
- OpenSBI tag
- BusyBox tag
- Container base image digest
- Toolchain package version
