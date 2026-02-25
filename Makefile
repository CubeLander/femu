RV32EMU_PERF_CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O3 -march=native
TRACE_OUT_DIR ?= $(CURDIR)/out/trace
TRACE_FILE ?= $(TRACE_OUT_DIR)/linux-boot.trace.log
TRACE_SUMMARY_JSON ?= $(TRACE_OUT_DIR)/linux-boot.summary.json
TRACE_OPENSBI ?= $(CURDIR)/out/opensbi/platform/generic/firmware/fw_dynamic.bin
TRACE_KERNEL_IMAGE ?= $(CURDIR)/out/linux/arch/riscv/boot/Image
TRACE_DTB ?= $(CURDIR)/out/smoke/virt-rv32-smoke.dtb
TRACE_INITRD ?= $(CURDIR)/out/rootfs/initramfs.cpio.gz
TRACE_MAX_INSTR ?= 1200000000
TRACE_TOP ?= 30
TRACE_TIMELINE ?= 80
TRACE_PHASE_WINDOW ?= 50000
TRACE_PHASE_MAX ?= 16
TRACE_SYMBOL_BIAS ?= 0x40000000
TRACE_FOCUS_PRIV ?= S

default: help

help:
	@echo "Workspace targets (independent rv32emu repo):"
	@echo "  make rv32emu-test|rv32emu-bin|rv32emu-bin-perf"
	@echo "  make build-linux|build-linux-smp|build-opensbi|build-rootfs|build-busybox-rv32"
	@echo "  make smoke-emulator|smoke-emulator-jit|smoke-emulator-smp|smoke-emulator-smp-linux"
	@echo "  make trace-linux-capture|trace-linux-summary|trace-linux-full"
	@echo "  make trace-linux-focus-sched|trace-linux-focus-net"
	@echo "  make dev-shell|check-env|bootstrap|build-all"
	 
clean:
	-$(MAKE) -C rv32emu clean

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

rv32emu-bin-perf:
	$(MAKE) -C rv32emu clean rv32emu CFLAGS='$(RV32EMU_PERF_CFLAGS)'

smoke-emulator: rv32emu-bin build-linux build-opensbi build-busybox-rv32 build-rootfs
	./scripts/smoke-emulator.sh

smoke-emulator-jit: rv32emu-bin build-linux build-opensbi build-busybox-rv32 build-rootfs
	RV32EMU_EXPERIMENTAL_JIT=1 RV32EMU_EXPERIMENTAL_JIT_HOT=1 ./scripts/smoke-emulator.sh

smoke-emulator-perf: rv32emu-bin-perf build-linux build-opensbi build-busybox-rv32 build-rootfs
	./scripts/smoke-emulator.sh

smoke-emulator-jit-perf: rv32emu-bin-perf build-linux build-opensbi build-busybox-rv32 build-rootfs
	RV32EMU_EXPERIMENTAL_JIT=1 RV32EMU_EXPERIMENTAL_JIT_HOT=1 ./scripts/smoke-emulator.sh

smoke-emulator-tb-perf: rv32emu-bin-perf build-linux build-opensbi build-busybox-rv32 build-rootfs
	RV32EMU_EXPERIMENTAL_TB=1 ./scripts/smoke-emulator.sh

smoke-emulator-strict: rv32emu-bin build-linux build-opensbi build-busybox-rv32 build-rootfs
	ALLOW_INIT_PANIC=0 ./scripts/smoke-emulator.sh

smoke-emulator-interactive: rv32emu-bin build-linux build-opensbi build-busybox-rv32 build-rootfs
	./scripts/smoke-emulator-interactive.sh

smoke-emulator-smp: rv32emu-bin build-linux-smp build-opensbi build-busybox-rv32 build-rootfs
	KERNEL_IMAGE="$(PWD)/out/linux-smp/arch/riscv/boot/Image" ./scripts/smoke-emulator-smp.sh

smoke-emulator-smp-linux: rv32emu-bin build-linux-smp build-opensbi build-busybox-rv32 build-rootfs
	KERNEL_IMAGE="$(PWD)/out/linux-smp/arch/riscv/boot/Image" \
	STAGE2_MAX_INSTR=1200000000 \
	STAGE2_ALLOW_INIT_PANIC=0 \
	STAGE2_REQUIRE_LINUX_BANNER=1 \
	STAGE2_REQUIRE_INIT_MARKER=1 \
	STAGE2_REQUIRE_SECONDARY_HART=1 \
	./scripts/smoke-emulator-smp.sh

smoke-emulator-smp-threaded: rv32emu-bin build-linux-smp build-opensbi build-busybox-rv32 build-rootfs
	KERNEL_IMAGE="$(PWD)/out/linux-smp/arch/riscv/boot/Image" \
	RV32EMU_EXPERIMENTAL_HART_THREADS=1 \
	./scripts/smoke-emulator-smp.sh

smoke-emulator-smp-linux-threaded: rv32emu-bin build-linux-smp build-opensbi build-busybox-rv32 build-rootfs
	KERNEL_IMAGE="$(PWD)/out/linux-smp/arch/riscv/boot/Image" \
	RV32EMU_EXPERIMENTAL_HART_THREADS=1 \
	STAGE2_MAX_INSTR=1200000000 \
	STAGE2_ALLOW_INIT_PANIC=0 \
	STAGE2_REQUIRE_LINUX_BANNER=1 \
	STAGE2_REQUIRE_INIT_MARKER=1 \
	STAGE2_REQUIRE_SECONDARY_HART=1 \
	./scripts/smoke-emulator-smp.sh

trace-linux-capture: rv32emu-bin
	@test -f "$(TRACE_OPENSBI)" || (echo "[ERR] missing $(TRACE_OPENSBI)"; exit 2)
	@test -f "$(TRACE_KERNEL_IMAGE)" || (echo "[ERR] missing $(TRACE_KERNEL_IMAGE)"; exit 2)
	@test -f "$(TRACE_DTB)" || (echo "[ERR] missing $(TRACE_DTB)"; exit 2)
	@test -f "$(TRACE_INITRD)" || (echo "[ERR] missing $(TRACE_INITRD)"; exit 2)
	mkdir -p "$(TRACE_OUT_DIR)"
	./rv32emu/build/rv32emu \
	  --opensbi "$(TRACE_OPENSBI)" \
	  --kernel "$(TRACE_KERNEL_IMAGE)" \
	  --dtb "$(TRACE_DTB)" \
	  --initrd "$(TRACE_INITRD)" \
	  --max-instr "$(TRACE_MAX_INSTR)" \
	  --trace \
	  --trace-file "$(TRACE_FILE)"

trace-linux-summary:
	@test -f "$(TRACE_FILE)" || (echo "[ERR] missing $(TRACE_FILE); run 'make trace-linux-capture' first"; exit 2)
	python3 ./rv32emu/scripts/trace_linux_path.py \
	  --trace "$(TRACE_FILE)" \
	  --system-map "$(CURDIR)/out/linux/System.map" \
	  --symbol-bias "$(TRACE_SYMBOL_BIAS)" \
	  --top "$(TRACE_TOP)" \
	  --timeline "$(TRACE_TIMELINE)" \
	  --phase-window "$(TRACE_PHASE_WINDOW)" \
	  --phase-max "$(TRACE_PHASE_MAX)" \
	  --json-out "$(TRACE_SUMMARY_JSON)"

trace-linux-focus-sched:
	@test -f "$(TRACE_FILE)" || (echo "[ERR] missing $(TRACE_FILE); run 'make trace-linux-capture' first"; exit 2)
	python3 ./rv32emu/scripts/trace_linux_path.py \
	  --trace "$(TRACE_FILE)" \
	  --system-map "$(CURDIR)/out/linux/System.map" \
	  --symbol-bias "$(TRACE_SYMBOL_BIAS)" \
	  $(if $(TRACE_FOCUS_PRIV),--priv "$(TRACE_FOCUS_PRIV)") \
	  --top "$(TRACE_TOP)" \
	  --timeline "$(TRACE_TIMELINE)" \
	  --phase-window "$(TRACE_PHASE_WINDOW)" \
	  --phase-max "$(TRACE_PHASE_MAX)" \
	  --focus-profile scheduler \
	  --json-out "$(TRACE_OUT_DIR)/linux-boot.scheduler.summary.json"

trace-linux-focus-net:
	@test -f "$(TRACE_FILE)" || (echo "[ERR] missing $(TRACE_FILE); run 'make trace-linux-capture' first"; exit 2)
	python3 ./rv32emu/scripts/trace_linux_path.py \
	  --trace "$(TRACE_FILE)" \
	  --system-map "$(CURDIR)/out/linux/System.map" \
	  --symbol-bias "$(TRACE_SYMBOL_BIAS)" \
	  $(if $(TRACE_FOCUS_PRIV),--priv "$(TRACE_FOCUS_PRIV)") \
	  --top "$(TRACE_TOP)" \
	  --timeline "$(TRACE_TIMELINE)" \
	  --phase-window "$(TRACE_PHASE_WINDOW)" \
	  --phase-max "$(TRACE_PHASE_MAX)" \
	  --focus-profile network \
	  --json-out "$(TRACE_OUT_DIR)/linux-boot.network.summary.json"

trace-linux-full: trace-linux-capture trace-linux-summary

bootstrap: build-toolchain fetch-sources build-busybox build-linux build-opensbi build-rootfs
	@echo "[OK] bootstrap artifacts are ready under out/"

build-all: build-busybox build-linux build-opensbi build-rootfs
	@echo "[OK] all build artifacts are ready under out/"

.PHONY: default help clean dev-shell fetch-sources build-toolchain install-rv32-toolchain build-busybox build-busybox-rv32 build-linux build-linux-smp build-opensbi build-rootfs smoke-qemu smoke-qemu-strict dump-dtb takeaway check-env check-boot-contract rv32emu-test rv32emu-bin rv32emu-bin-perf smoke-emulator smoke-emulator-jit smoke-emulator-perf smoke-emulator-jit-perf smoke-emulator-tb-perf smoke-emulator-strict smoke-emulator-interactive smoke-emulator-smp smoke-emulator-smp-linux smoke-emulator-smp-threaded smoke-emulator-smp-linux-threaded trace-linux-capture trace-linux-summary trace-linux-focus-sched trace-linux-focus-net trace-linux-full bootstrap build-all
