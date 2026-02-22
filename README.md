# ICS2019 Programming Assignment

This project is the programming assignment of the class ICS(Introduction to Computer System) in Department of Computer Science and Technology, Nanjing University.

For the guide of this programming assignment,
refer to http://nju-ics.gitbooks.io/ics2019-programming-assignment/content/

To initialize, run
```bash
bash init.sh
```

The following subprojects/components are included. Some of them are not fully implemented.
* [NEMU](https://github.com/NJU-ProjectN/nemu)
* [Nexus-am](https://github.com/NJU-ProjectN/nexus-am)
* [Nanos-lite](https://github.com/NJU-ProjectN/nanos-lite)
* [Navy-apps](https://github.com/NJU-ProjectN/navy-apps)

## Migration/Automation Additions

To support migration into a clean RV32 Linux emulator repository, this repo now includes:

* `issues/0001-comprehensive-report.md` - integrated technical report
* `docs/migration/` - dependency matrix + new-repo bootstrap guide
* `docker/Dockerfile.dev` - containerized development environment
* `scripts/` - automated build/takeaway scripts

Quick entry points:

```bash
make dev-shell
make fetch-sources
make build-toolchain
make install-rv32-toolchain
make build-busybox
make build-busybox-rv32
make build-linux
make build-opensbi
make build-rootfs
make build-all
make bootstrap
make smoke-qemu
make smoke-qemu-strict
make takeaway
```

Suggested first-time flow in dev container:

```bash
make check-env
make fetch-sources
make build-busybox
make build-linux
make build-opensbi
make build-rootfs
make smoke-qemu
```

Strict mode (fail on kernel panic after init handoff):

```bash
ALLOW_INIT_PANIC=0 make smoke-qemu
```

Recommended rv32 end-to-end smoke:

```bash
make install-rv32-toolchain
make build-busybox-rv32
make smoke-qemu-strict
```

If cross compiler is missing, run:

```bash
make build-toolchain
```

Windows/WSL2 users should run through:

`docs/migration/wsl2-docker-setup.md`

If you are behind GFW/Clash, also see:

`docs/migration/network-proxy.md`
