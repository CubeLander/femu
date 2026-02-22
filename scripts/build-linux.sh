#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LINUX_SRC="${ROOT_DIR}/linux"
OUT_DIR="${OUT_DIR:-${ROOT_DIR}/out/linux}"
JOBS="${JOBS:-$(nproc)}"
ARCH_NAME="${ARCH_NAME:-riscv}"
CROSS_PREFIX="${CROSS_COMPILE:-}"
CONFIG_FRAGMENT="${ROOT_DIR}/scripts/config/linux-rv32-minimal.config"
BASE_DEFCONFIG="${BASE_DEFCONFIG:-rv32_defconfig}"

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

if [[ ! -d "${LINUX_SRC}" ]]; then
  echo "[ERR] linux source not found: ${LINUX_SRC}" >&2
  exit 1
fi

detect_cross_prefix
echo "[INFO] CROSS_COMPILE=${CROSS_PREFIX}"

mkdir -p "${OUT_DIR}"

CONFIG_NEEDS_INIT=0
if [[ ! -f "${OUT_DIR}/.config" ]]; then
  CONFIG_NEEDS_INIT=1
elif [[ "${BASE_DEFCONFIG}" == "rv32_defconfig" ]] && ! grep -q "^CONFIG_32BIT=y" "${OUT_DIR}/.config"; then
  echo "[WARN] existing kernel config is not rv32; regenerating from ${BASE_DEFCONFIG}"
  CONFIG_NEEDS_INIT=1
fi

if [[ "${CONFIG_NEEDS_INIT}" == "1" ]]; then
  DEFCONFIG_TARGET="${BASE_DEFCONFIG}"
  if [[ ! -f "${LINUX_SRC}/arch/${ARCH_NAME}/configs/${BASE_DEFCONFIG}" ]]; then
    echo "[WARN] ${BASE_DEFCONFIG} not found; falling back to defconfig"
    DEFCONFIG_TARGET="defconfig"
  fi

  echo "[INFO] generating base config: ${DEFCONFIG_TARGET}"
  make -C "${LINUX_SRC}" O="${OUT_DIR}" ARCH="${ARCH_NAME}" CROSS_COMPILE="${CROSS_PREFIX}" "${DEFCONFIG_TARGET}"

  if [[ -f "${CONFIG_FRAGMENT}" ]]; then
    echo "[INFO] merging rv32 minimal fragment"
    "${LINUX_SRC}/scripts/kconfig/merge_config.sh" -m -O "${OUT_DIR}" \
      "${OUT_DIR}/.config" "${CONFIG_FRAGMENT}"
    make -C "${LINUX_SRC}" O="${OUT_DIR}" ARCH="${ARCH_NAME}" CROSS_COMPILE="${CROSS_PREFIX}" olddefconfig
  fi
fi

if ! grep -q "^CONFIG_32BIT=y" "${OUT_DIR}/.config"; then
  echo "[ERR] kernel config is not rv32 (CONFIG_32BIT!=y)." >&2
  echo "      remove ${OUT_DIR} and rebuild, or set BASE_DEFCONFIG=rv32_defconfig." >&2
  exit 1
fi

echo "[INFO] kernel config confirms rv32"
if [[ -f "${CONFIG_FRAGMENT}" ]]; then
  # Informative check so users can spot command-line drift early.
  if ! grep -q '^CONFIG_CMDLINE="console=ttyS0 earlycon=sbi rdinit=/bin/sh"' "${OUT_DIR}/.config"; then
    echo "[WARN] CONFIG_CMDLINE differs from expected minimal fragment."
  fi
fi

echo "[INFO] building Linux Image"
make -C "${LINUX_SRC}" O="${OUT_DIR}" ARCH="${ARCH_NAME}" CROSS_COMPILE="${CROSS_PREFIX}" -j"${JOBS}" Image

echo "[OK] Linux image: ${OUT_DIR}/arch/riscv/boot/Image"
