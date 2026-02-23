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

build-linux-smp:
	OUT_DIR="$(PWD)/out/linux-smp" EXTRA_CONFIG_FRAGMENT="$(PWD)/scripts/config/linux-rv32-smp.config" EXPECT_SMP=1 ./scripts/build-linux.sh

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

check-boot-contract:
	./scripts/check-boot-contract.sh

rv32emu-test:
	$(MAKE) -C rv32emu test

rv32emu-bin:
	$(MAKE) -C rv32emu rv32emu

smoke-emulator: rv32emu-bin build-linux build-opensbi build-busybox-rv32 build-rootfs
	./scripts/smoke-emulator.sh

smoke-emulator-strict: rv32emu-bin build-linux build-opensbi build-busybox-rv32 build-rootfs
	ALLOW_INIT_PANIC=0 ./scripts/smoke-emulator.sh

smoke-emulator-interactive: rv32emu-bin build-linux build-opensbi build-busybox-rv32 build-rootfs
	./scripts/smoke-emulator-interactive.sh

smoke-emulator-smp: rv32emu-bin build-linux-smp build-opensbi build-busybox-rv32 build-rootfs
	KERNEL_IMAGE="$(PWD)/out/linux-smp/arch/riscv/boot/Image" HART_COUNT=2 DTB_HART_COUNT=2 TIMEOUT_SEC=120 MAX_INSTR=120000000 REQUIRE_LINUX_BANNER=0 REQUIRE_INIT_MARKER=0 REQUIRE_SECONDARY_HART=1 SECONDARY_HART_ID=1 ./scripts/smoke-emulator.sh

bootstrap: build-toolchain fetch-sources build-busybox build-linux build-opensbi build-rootfs
	@echo "[OK] bootstrap artifacts are ready under out/"

build-all: build-busybox build-linux build-opensbi build-rootfs
	@echo "[OK] all build artifacts are ready under out/"

.PHONY: default clean submit info setup password dev-shell fetch-sources build-toolchain install-rv32-toolchain build-busybox build-busybox-rv32 build-linux build-linux-smp build-opensbi build-rootfs smoke-qemu smoke-qemu-strict dump-dtb takeaway check-env check-boot-contract rv32emu-test rv32emu-bin smoke-emulator smoke-emulator-strict smoke-emulator-interactive smoke-emulator-smp bootstrap build-all
