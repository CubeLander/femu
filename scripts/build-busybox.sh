#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUSYBOX_SRC="${BUSYBOX_SRC:-${ROOT_DIR}/busybox}"
OUT_DIR="${OUT_DIR:-${ROOT_DIR}/out/busybox}"
JOBS="${JOBS:-$(nproc)}"
ARCH_NAME="${ARCH_NAME:-riscv}"
CROSS_PREFIX="${CROSS_COMPILE:-}"

run_kconfig_noninteractive() {
  set +o pipefail
  yes "" | make "$@"
  local make_status="${PIPESTATUS[1]}"
  set -o pipefail
  return "${make_status}"
}

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

if [[ ! -d "${BUSYBOX_SRC}" ]]; then
  echo "[ERR] busybox source not found: ${BUSYBOX_SRC}" >&2
  echo "      run ./scripts/fetch-sources.sh first." >&2
  exit 1
fi

detect_cross_prefix
echo "[INFO] CROSS_COMPILE=${CROSS_PREFIX}"

mkdir -p "${OUT_DIR}"

if [[ ! -f "${OUT_DIR}/.config" ]]; then
  echo "[INFO] generating busybox defconfig"
  run_kconfig_noninteractive -C "${BUSYBOX_SRC}" O="${OUT_DIR}" ARCH="${ARCH_NAME}" CROSS_COMPILE="${CROSS_PREFIX}" defconfig >/dev/null
fi

if [[ -x "${BUSYBOX_SRC}/scripts/config" ]]; then
  "${BUSYBOX_SRC}/scripts/config" --file "${OUT_DIR}/.config" -e STATIC
  "${BUSYBOX_SRC}/scripts/config" --file "${OUT_DIR}/.config" -d TC
else
  if grep -q "^# CONFIG_STATIC is not set" "${OUT_DIR}/.config"; then
    sed -i "s/^# CONFIG_STATIC is not set$/CONFIG_STATIC=y/" "${OUT_DIR}/.config"
  elif ! grep -q "^CONFIG_STATIC=" "${OUT_DIR}/.config"; then
    echo "CONFIG_STATIC=y" >> "${OUT_DIR}/.config"
  fi

  if grep -q "^CONFIG_TC=y" "${OUT_DIR}/.config"; then
    sed -i "s/^CONFIG_TC=y$/# CONFIG_TC is not set/" "${OUT_DIR}/.config"
  elif ! grep -q "^# CONFIG_TC is not set" "${OUT_DIR}/.config"; then
    echo "# CONFIG_TC is not set" >> "${OUT_DIR}/.config"
  fi
fi

run_kconfig_noninteractive -C "${BUSYBOX_SRC}" O="${OUT_DIR}" ARCH="${ARCH_NAME}" CROSS_COMPILE="${CROSS_PREFIX}" oldconfig >/dev/null
make -C "${BUSYBOX_SRC}" O="${OUT_DIR}" ARCH="${ARCH_NAME}" CROSS_COMPILE="${CROSS_PREFIX}" -j"${JOBS}" busybox

if [[ ! -x "${OUT_DIR}/busybox" ]]; then
  echo "[ERR] expected busybox binary missing: ${OUT_DIR}/busybox" >&2
  exit 1
fi

BB_DESC="$(file -b "${OUT_DIR}/busybox")"
if echo "${BB_DESC}" | grep -qi "64-bit"; then
  echo "[WARN] busybox output is 64-bit: ${OUT_DIR}/busybox"
  echo "      rv32 smoke will reach kernel but fail at init handoff."
  echo "      use an rv32-capable userspace toolchain if you need strict pass."
fi

echo "[OK] busybox binary: ${OUT_DIR}/busybox"
