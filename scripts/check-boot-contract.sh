#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

DRAM_BASE="${DRAM_BASE:-0x80000000}"
RAM_MB="${RAM_MB:-256}"

OPENSBI_FW="${OPENSBI_FW:-${ROOT_DIR}/out/opensbi/platform/generic/firmware/fw_dynamic.bin}"
KERNEL_IMAGE="${KERNEL_IMAGE:-${ROOT_DIR}/out/linux/arch/riscv/boot/Image}"
KERNEL_VMLINUX="${KERNEL_VMLINUX:-${ROOT_DIR}/out/linux/vmlinux}"
DTB_FILE="${DTB_FILE:-${ROOT_DIR}/out/virt-rv32.dtb}"
INITRAMFS="${INITRAMFS:-${ROOT_DIR}/out/rootfs/initramfs.cpio.gz}"
BUSYBOX_BIN="${BUSYBOX_BIN:-${ROOT_DIR}/out/busybox/busybox}"

OPENSBI_LOAD="${OPENSBI_LOAD:-0x80000000}"
KERNEL_LOAD="${KERNEL_LOAD:-0x80400000}"
DTB_LOAD="${DTB_LOAD:-0x87f00000}"
INITRD_LOAD="${INITRD_LOAD:-0x88000000}"

AUTO_DUMP_DTB="${AUTO_DUMP_DTB:-1}"
EXPECTED_CMDLINE='console=ttyS0 earlycon=sbi rdinit=/bin/sh'

round_up() {
  local value="$1"
  local align="$2"
  echo $(( (value + align - 1) / align * align ))
}

hex32() {
  printf '0x%08x' "$1"
}

size_of() {
  local path="$1"
  stat -c '%s' "${path}"
}

require_file() {
  local path="$1"
  if [[ ! -f "${path}" ]]; then
    echo "[ERR] missing file: ${path}" >&2
    return 1
  fi
}

check_elf32() {
  local path="$1"
  local name="$2"
  if ! file -b "${path}" | grep -q 'ELF 32-bit'; then
    echo "[ERR] ${name} is not ELF 32-bit: ${path}" >&2
    file "${path}" >&2 || true
    return 1
  fi
  echo "[OK] ${name} is ELF 32-bit"
}

check_kernel_32() {
  local image_path="$1"
  local vmlinux_path="$2"

  if ! file -b "${image_path}" | grep -Eq 'RISC-V 32-bit|ELF 32-bit'; then
    echo "[ERR] kernel image is not rv32: ${image_path}" >&2
    file "${image_path}" >&2 || true
    return 1
  fi
  echo '[OK] kernel image looks rv32-compatible'

  if [[ -f "${vmlinux_path}" ]]; then
    if ! readelf -h "${vmlinux_path}" | grep -q 'Class:.*ELF32'; then
      echo "[ERR] vmlinux is not ELF32: ${vmlinux_path}" >&2
      return 1
    fi
    echo '[OK] vmlinux is ELF32'
  else
    echo "[WARN] vmlinux not found (skip ELF32 kernel check): ${vmlinux_path}"
  fi
}

if [[ ! -f "${DTB_FILE}" && "${AUTO_DUMP_DTB}" == "1" ]]; then
  echo "[INFO] dtb missing, trying auto dump: ${DTB_FILE}"
  "${ROOT_DIR}/scripts/dump-virt-dtb.sh" "${DTB_FILE}"
fi

require_file "${OPENSBI_FW}"
require_file "${KERNEL_IMAGE}"
require_file "${DTB_FILE}"
require_file "${INITRAMFS}"

check_kernel_32 "${KERNEL_IMAGE}" "${KERNEL_VMLINUX}"
if [[ -x "${BUSYBOX_BIN}" ]]; then
  check_elf32 "${BUSYBOX_BIN}" "busybox"
else
  echo "[WARN] busybox binary not found (skip arch check): ${BUSYBOX_BIN}"
fi

DRAM_BASE_DEC=$((DRAM_BASE))
DRAM_SIZE_DEC=$((RAM_MB * 1024 * 1024))
DRAM_END_DEC=$((DRAM_BASE_DEC + DRAM_SIZE_DEC))

OPENSBI_LOAD_DEC=$((OPENSBI_LOAD))
KERNEL_LOAD_DEC=$((KERNEL_LOAD))
DTB_LOAD_DEC=$((DTB_LOAD))
INITRD_LOAD_DEC=$((INITRD_LOAD))

OPENSBI_SIZE_DEC="$(size_of "${OPENSBI_FW}")"
KERNEL_SIZE_DEC="$(size_of "${KERNEL_IMAGE}")"
DTB_SIZE_DEC="$(size_of "${DTB_FILE}")"
INITRD_SIZE_DEC="$(size_of "${INITRAMFS}")"

# Keep OpenSBI region page-aligned for simple overlap checking.
OPENSBI_REGION_SIZE_DEC="$(round_up "${OPENSBI_SIZE_DEC}" 4096)"

names=("opensbi" "kernel" "dtb" "initramfs")
bases=("${OPENSBI_LOAD_DEC}" "${KERNEL_LOAD_DEC}" "${DTB_LOAD_DEC}" "${INITRD_LOAD_DEC}")
sizes=("${OPENSBI_REGION_SIZE_DEC}" "${KERNEL_SIZE_DEC}" "${DTB_SIZE_DEC}" "${INITRD_SIZE_DEC}")

printf '[INFO] DRAM range: %s - %s (%u MiB)\n' "$(hex32 "${DRAM_BASE_DEC}")" "$(hex32 "$((DRAM_END_DEC - 1))")" "${RAM_MB}"
printf '%-10s %-12s %-12s %-12s\n' "name" "start" "end" "size(bytes)"

for i in "${!names[@]}"; do
  start="${bases[$i]}"
  size="${sizes[$i]}"
  end=$((start + size))

  printf '%-10s %-12s %-12s %-12u\n' "${names[$i]}" "$(hex32 "${start}")" "$(hex32 "$((end - 1))")" "${size}"

  if (( start < DRAM_BASE_DEC || end > DRAM_END_DEC )); then
    echo "[ERR] ${names[$i]} out of DRAM range" >&2
    exit 1
  fi

done

for ((i = 0; i < ${#names[@]}; i++)); do
  for ((j = i + 1; j < ${#names[@]}; j++)); do
    s1="${bases[$i]}"; e1=$((s1 + sizes[$i]))
    s2="${bases[$j]}"; e2=$((s2 + sizes[$j]))
    if (( s1 < e2 && s2 < e1 )); then
      echo "[ERR] region overlap: ${names[$i]} <-> ${names[$j]}" >&2
      exit 1
    fi
  done

done

if [[ -f "${ROOT_DIR}/out/linux/.config" ]]; then
  if grep -q '^CONFIG_32BIT=y' "${ROOT_DIR}/out/linux/.config"; then
    echo '[OK] kernel config has CONFIG_32BIT=y'
  else
    echo '[ERR] kernel config missing CONFIG_32BIT=y' >&2
    exit 1
  fi

  if grep -q '^CONFIG_FPU=y' "${ROOT_DIR}/out/linux/.config"; then
    echo '[OK] kernel config has CONFIG_FPU=y'
  else
    echo '[ERR] kernel config missing CONFIG_FPU=y (required by current ilp32d userspace)' >&2
    exit 1
  fi

  if grep -q "^CONFIG_CMDLINE=\"${EXPECTED_CMDLINE}\"" "${ROOT_DIR}/out/linux/.config"; then
    echo '[OK] kernel cmdline matches boot contract'
  else
    echo '[WARN] kernel cmdline differs from default boot contract'
  fi
else
  echo "[WARN] ${ROOT_DIR}/out/linux/.config missing (skip kernel config checks)"
fi

echo '[OK] boot contract check passed'
