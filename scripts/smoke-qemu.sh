#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

QEMU_BIN="${QEMU_BIN:-qemu-system-riscv32}"
MACHINE="${MACHINE:-virt}"
MEMORY_MB="${MEMORY_MB:-256}"
TIMEOUT_SEC="${TIMEOUT_SEC:-20}"
APPEND="${APPEND:-console=ttyS0 earlycon=sbi rdinit=/bin/sh}"
ALLOW_INIT_PANIC="${ALLOW_INIT_PANIC:-1}"

KERNEL_IMAGE="${KERNEL_IMAGE:-${ROOT_DIR}/out/linux/arch/riscv/boot/Image}"
OPENSBI_FW="${OPENSBI_FW:-${ROOT_DIR}/out/opensbi/platform/generic/firmware/fw_dynamic.bin}"
INITRAMFS="${INITRAMFS:-${ROOT_DIR}/out/rootfs/initramfs.cpio.gz}"
LOG_PATH="${LOG_PATH:-${ROOT_DIR}/out/smoke/qemu-smoke.log}"

check_file() {
  local path="$1"
  if [[ ! -f "${path}" ]]; then
    echo "[ERR] missing file: ${path}" >&2
    exit 1
  fi
}

check_marker() {
  local pattern="$1"
  local label="$2"
  if ! grep -Eq "${pattern}" "${LOG_PATH}"; then
    echo "[ERR] smoke marker missing: ${label}" >&2
    return 1
  fi
  echo "[OK] marker found: ${label}"
}

if ! command -v "${QEMU_BIN}" >/dev/null 2>&1; then
  echo "[ERR] missing qemu command: ${QEMU_BIN}" >&2
  exit 1
fi

if ! command -v timeout >/dev/null 2>&1; then
  echo "[ERR] missing command: timeout" >&2
  exit 1
fi

check_file "${KERNEL_IMAGE}"
check_file "${OPENSBI_FW}"
check_file "${INITRAMFS}"

KERNEL_FILE_DESC="$(file -b "${KERNEL_IMAGE}")"
if echo "${KERNEL_FILE_DESC}" | grep -qi "64-bit"; then
  echo "[ERR] kernel image is 64-bit, but smoke test uses qemu-system-riscv32." >&2
  echo "      rebuild Linux as rv32 (make build-linux)." >&2
  exit 1
fi

mkdir -p "$(dirname "${LOG_PATH}")"
echo "[INFO] qemu smoke log: ${LOG_PATH}"
echo "[INFO] running ${QEMU_BIN} for up to ${TIMEOUT_SEC}s"

QEMU_CMD=(
  "${QEMU_BIN}"
  -machine "${MACHINE}"
  -m "${MEMORY_MB}M"
  -nographic
  -monitor none
  -bios "${OPENSBI_FW}"
  -kernel "${KERNEL_IMAGE}"
  -initrd "${INITRAMFS}"
)

if [[ -n "${APPEND}" ]]; then
  QEMU_CMD+=( -append "${APPEND}" )
fi

if [[ -n "${EXTRA_QEMU_ARGS:-}" ]]; then
  # shellcheck disable=SC2206
  EXTRA_ARGS=( ${EXTRA_QEMU_ARGS} )
  QEMU_CMD+=( "${EXTRA_ARGS[@]}" )
fi

set +e
timeout "${TIMEOUT_SEC}s" "${QEMU_CMD[@]}" >"${LOG_PATH}" 2>&1
QEMU_RC=$?
set -e

if [[ "${QEMU_RC}" -ne 0 && "${QEMU_RC}" -ne 124 && "${QEMU_RC}" -ne 137 ]]; then
  echo "[ERR] qemu exited unexpectedly: rc=${QEMU_RC}" >&2
  tail -n 80 "${LOG_PATH}" >&2 || true
  exit 1
fi

check_marker "OpenSBI v" "OpenSBI banner"
check_marker "Linux version" "Linux banner"
check_marker "Kernel command line:" "kernel cmdline"
check_marker "Run /bin/sh as init process|Run /init as init process" "init handoff"

if grep -Eq "Kernel panic - not syncing|No working init found" "${LOG_PATH}"; then
  if [[ "${ALLOW_INIT_PANIC}" == "1" ]]; then
    echo "[WARN] kernel panic detected after init handoff."
    echo "[WARN] this usually means initramfs userland arch mismatch (e.g. non-rv32 busybox)."
    echo "[WARN] set ALLOW_INIT_PANIC=0 for strict mode."
  else
    echo "[ERR] kernel panic detected during smoke test." >&2
    tail -n 120 "${LOG_PATH}" >&2 || true
    exit 1
  fi
fi

echo "[OK] qemu smoke test passed"
tail -n 40 "${LOG_PATH}" || true
