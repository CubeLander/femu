#!/usr/bin/env bash
set -euo pipefail

OUT_PATH="${1:-virt-rv32.dtb}"
QEMU_BIN="${QEMU_BIN:-qemu-system-riscv32}"
MACHINE="${MACHINE:-virt}"
SMP="${SMP:-1}"

if ! [[ "${SMP}" =~ ^[0-9]+$ ]] || (( SMP < 1 )); then
  echo "[ERR] invalid SMP value: ${SMP}" >&2
  exit 1
fi

if ! command -v "${QEMU_BIN}" >/dev/null 2>&1; then
  echo "[ERR] qemu-system-riscv32 not found: ${QEMU_BIN}" >&2
  exit 1
fi

timeout 2s "${QEMU_BIN}" \
  -machine "${MACHINE},dumpdtb=${OUT_PATH}" \
  -smp "${SMP}" \
  -nographic -bios none >/dev/null 2>&1 || true

if [[ ! -f "${OUT_PATH}" ]]; then
  echo "[ERR] failed to dump dtb to ${OUT_PATH}" >&2
  exit 1
fi

echo "[OK] dtb dumped: ${OUT_PATH} (smp=${SMP})"
