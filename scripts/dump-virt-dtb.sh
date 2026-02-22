#!/usr/bin/env bash
set -euo pipefail

OUT_PATH="${1:-virt-rv32.dtb}"

if ! command -v qemu-system-riscv32 >/dev/null 2>&1; then
  echo "[ERR] qemu-system-riscv32 not found" >&2
  exit 1
fi

timeout 2s qemu-system-riscv32 \
  -machine virt,dumpdtb="${OUT_PATH}" \
  -nographic -bios none >/dev/null 2>&1 || true

if [[ ! -f "${OUT_PATH}" ]]; then
  echo "[ERR] failed to dump dtb to ${OUT_PATH}" >&2
  exit 1
fi

echo "[OK] dtb dumped: ${OUT_PATH}"
