#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OPENSBI_SRC="${OPENSBI_SRC:-${ROOT_DIR}/out/sources/opensbi}"
OUT_DIR="${OUT_DIR:-${ROOT_DIR}/out/opensbi}"
JOBS="${JOBS:-$(nproc)}"
CROSS_PREFIX="${CROSS_COMPILE:-}"

detect_cross_prefix() {
  if [[ -n "${CROSS_PREFIX}" ]]; then
    return 0
  fi

  if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    CROSS_PREFIX="riscv64-linux-gnu-"
    return 0
  fi

  if command -v riscv64-unknown-linux-gnu-gcc >/dev/null 2>&1; then
    CROSS_PREFIX="riscv64-unknown-linux-gnu-"
    return 0
  fi

  echo "[ERR] no riscv cross compiler found." >&2
  echo "      run ./scripts/build-toolchain.sh or use docker dev shell." >&2
  exit 1
}

if [[ ! -d "${OPENSBI_SRC}" ]]; then
  echo "[ERR] opensbi source not found: ${OPENSBI_SRC}" >&2
  echo "      run ./scripts/fetch-sources.sh first or set OPENSBI_SRC." >&2
  exit 1
fi

detect_cross_prefix
echo "[INFO] CROSS_COMPILE=${CROSS_PREFIX}"

mkdir -p "${OUT_DIR}"

echo "[INFO] building OpenSBI FW_DYNAMIC (rv32 generic)"
make -C "${OPENSBI_SRC}" \
  O="${OUT_DIR}" \
  PLATFORM=generic \
  PLATFORM_RISCV_XLEN=32 \
  CROSS_COMPILE="${CROSS_PREFIX}" \
  FW_DYNAMIC=y \
  -j"${JOBS}"

ARTIFACT="${OUT_DIR}/platform/generic/firmware/fw_dynamic.bin"
if [[ ! -f "${ARTIFACT}" ]]; then
  echo "[ERR] expected artifact missing: ${ARTIFACT}" >&2
  exit 1
fi

echo "[OK] OpenSBI firmware: ${ARTIFACT}"
