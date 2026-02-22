-include nemu/Makefile.git

default:
	@echo "Please run 'make' under any subprojects to compile."
 
clean:
	-$(MAKE) -C nemu clean
	-$(MAKE) -C nexus-am clean
	-$(MAKE) -C nanos-lite clean
	-$(MAKE) -C navy-apps clean

submit: 
	STUID=$(STUID) STUNAME=$(STUNAME) bash -c "$$(curl -s https://course.cunok.cn:52443/pa/scripts/submit.sh)"

info: 
	STUID=$(STUID) STUNAME=$(STUNAME) bash -c "$$(curl -s https://course.cunok.cn:52443/pa/scripts/info.sh)"

setup: 
	STUID=$(STUID) STUNAME=$(STUNAME) bash -c "$$(curl -s https://course.cunok.cn:52443/pa/scripts/setup.sh)"

password: 
	STUID=$(STUID) STUNAME=$(STUNAME) bash -c "$$(curl -s https://course.cunok.cn:52443/pa/scripts/password.sh)"

dev-shell:
	./scripts/dev-shell.sh

fetch-sources:
	./scripts/fetch-sources.sh

build-toolchain:
	./scripts/build-toolchain.sh

install-rv32-toolchain:
	./scripts/install-rv32-toolchain.sh

build-busybox:
	./scripts/build-busybox.sh

build-busybox-rv32: install-rv32-toolchain
	CROSS_COMPILE="$(PWD)/opt/toolchains/riscv32-ilp32d--glibc--stable-2025.08-1/bin/riscv32-linux-" REQUIRE_RV32=1 ./scripts/build-busybox.sh

build-linux:
	./scripts/build-linux.sh

build-opensbi:
	./scripts/build-opensbi.sh

build-rootfs:
	./scripts/build-rootfs.sh

smoke-qemu: build-linux build-opensbi build-rootfs
	./scripts/smoke-qemu.sh

smoke-qemu-strict: build-linux build-opensbi build-busybox-rv32 build-rootfs
	ALLOW_INIT_PANIC=0 ./scripts/smoke-qemu.sh

dump-dtb:
	./scripts/dump-virt-dtb.sh out/virt-rv32.dtb

takeaway:
	./scripts/takeaway.sh

check-env:
	./scripts/check-env.sh

bootstrap: build-toolchain fetch-sources build-busybox build-linux build-opensbi build-rootfs
	@echo "[OK] bootstrap artifacts are ready under out/"

build-all: build-busybox build-linux build-opensbi build-rootfs
	@echo "[OK] all build artifacts are ready under out/"

.PHONY: default clean submit info setup password dev-shell fetch-sources build-toolchain install-rv32-toolchain build-busybox build-busybox-rv32 build-linux build-opensbi build-rootfs smoke-qemu smoke-qemu-strict dump-dtb takeaway check-env bootstrap build-all
