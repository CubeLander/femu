#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_BASE="${OUT_BASE:-${ROOT_DIR}/out/takeaway}"
STAMP="$(date +%Y%m%d-%H%M%S)"
PKG_DIR="${OUT_BASE}/pkg-${STAMP}"
TARBALL="${OUT_BASE}/ics2019-takeaway-${STAMP}.tar.gz"

mkdir -p "${PKG_DIR}" "${OUT_BASE}"

copy_if_exists() {
  local src="$1"
  if [[ -e "${ROOT_DIR}/${src}" ]]; then
    rsync -a "${ROOT_DIR}/${src}" "${PKG_DIR}/"
  fi
}

copy_if_exists "issues"
copy_if_exists "docs"
copy_if_exists "scripts"
copy_if_exists "docker"
copy_if_exists "rv32emu"
copy_if_exists "README.md"
copy_if_exists "Makefile"
copy_if_exists ".gitignore"

if [[ "${WITH_ARTIFACTS:-0}" == "1" ]]; then
  copy_if_exists "linux/arch/riscv/boot/Image"
  copy_if_exists "opensbi-riscv32-generic-fw_dynamic.bin"
  copy_if_exists "initramfs.cpio.gz"
fi

tar -czf "${TARBALL}" -C "${PKG_DIR}" .

echo "[OK] takeaway package created: ${TARBALL}"
