#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

EMU_BIN="${EMU_BIN:-${ROOT_DIR}/rv32emu/build/rv32emu}"
TIMEOUT_SEC="${TIMEOUT_SEC:-90}"
MEMORY_MB="${MEMORY_MB:-256}"
MAX_INSTR="${MAX_INSTR:-1200000000}"
APPEND="${APPEND:-console=ttyS0 earlycon=sbi rdinit=/init}"
ALLOW_INIT_PANIC="${ALLOW_INIT_PANIC:-1}"
CPU_ISA="${CPU_ISA:-rv32imafdc_zicsr_zifencei}"
HART_COUNT="${HART_COUNT:-1}"
DTB_HART_COUNT="${DTB_HART_COUNT:-${HART_COUNT}}"
REQUIRE_LINUX_BANNER="${REQUIRE_LINUX_BANNER:-1}"
REQUIRE_INIT_MARKER="${REQUIRE_INIT_MARKER:-1}"
REQUIRE_SECONDARY_HART="${REQUIRE_SECONDARY_HART:-0}"
SECONDARY_HART_ID="${SECONDARY_HART_ID:-1}"

OPENSBI_FW="${OPENSBI_FW:-${ROOT_DIR}/out/opensbi/platform/generic/firmware/fw_dynamic.bin}"
KERNEL_IMAGE="${KERNEL_IMAGE:-${ROOT_DIR}/out/linux/arch/riscv/boot/Image}"
DTB_IN="${DTB_IN:-${ROOT_DIR}/out/virt-rv32.dtb}"
INITRAMFS="${INITRAMFS:-${ROOT_DIR}/out/rootfs/initramfs.cpio.gz}"

OPENSBI_LOAD="${OPENSBI_LOAD:-0x80000000}"
KERNEL_LOAD="${KERNEL_LOAD:-0x80400000}"
DTB_LOAD="${DTB_LOAD:-0x87f00000}"
INITRD_LOAD="${INITRD_LOAD:-0x88000000}"
FW_DYNAMIC_INFO="${FW_DYNAMIC_INFO:-0x803ff000}"

LOG_PATH="${LOG_PATH:-${ROOT_DIR}/out/smoke/emulator-smoke.log}"
SMOKE_DTB="${SMOKE_DTB:-${ROOT_DIR}/out/smoke/virt-rv32-smoke.dtb}"

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

check_positive_int() {
  local value="$1"
  local label="$2"

  if ! [[ "${value}" =~ ^[0-9]+$ ]] || (( value < 1 )); then
    echo "[ERR] invalid ${label}: ${value}" >&2
    exit 1
  fi
}

generate_dtb_input() {
  SMP="${DTB_HART_COUNT}" "${ROOT_DIR}/scripts/dump-virt-dtb.sh" "${DTB_IN}"
}

ensure_dtb_input() {
  local last_hart=$((DTB_HART_COUNT - 1))

  if [[ -f "${DTB_IN}" ]]; then
    if fdtget "${DTB_IN}" "/cpus/cpu@${last_hart}" reg >/dev/null 2>&1; then
      return 0
    fi
    echo "[WARN] DTB cpu topology is too small for DTB_HART_COUNT=${DTB_HART_COUNT}; regenerating"
    generate_dtb_input
    return 0
  fi
  echo "[WARN] DTB not found, trying auto-generate via dump-virt-dtb.sh"
  generate_dtb_input
}

prepare_dtb() {
  local initrd_start=$((INITRD_LOAD))
  local initrd_end=$((INITRD_LOAD + INITRD_SIZE))
  local mem_size=$((MEMORY_MB * 1024 * 1024))
  local initrd_start_hi=$(( (initrd_start >> 32) & 0xffffffff ))
  local initrd_start_lo=$(( initrd_start & 0xffffffff ))
  local initrd_end_hi=$(( (initrd_end >> 32) & 0xffffffff ))
  local initrd_end_lo=$(( initrd_end & 0xffffffff ))
  local mem_size_hi=$(( (mem_size >> 32) & 0xffffffff ))
  local mem_size_lo=$(( mem_size & 0xffffffff ))
  local initrd_start_hi_hex
  local initrd_start_lo_hex
  local initrd_end_hi_hex
  local initrd_end_lo_hex
  local mem_size_hi_hex
  local mem_size_lo_hex
  local hart
  local cpu_node

  mkdir -p "$(dirname "${SMOKE_DTB}")"
  cp "${DTB_IN}" "${SMOKE_DTB}"

  printf -v initrd_start_hi_hex '%x' "${initrd_start_hi}"
  printf -v initrd_start_lo_hex '%x' "${initrd_start_lo}"
  printf -v initrd_end_hi_hex '%x' "${initrd_end_hi}"
  printf -v initrd_end_lo_hex '%x' "${initrd_end_lo}"
  printf -v mem_size_hi_hex '%x' "${mem_size_hi}"
  printf -v mem_size_lo_hex '%x' "${mem_size_lo}"

  # Mirror what qemu -append/-initrd does by patching /chosen.
  fdtput -t s "${SMOKE_DTB}" /chosen bootargs "${APPEND}"
  fdtput -t x "${SMOKE_DTB}" /chosen linux,initrd-start \
    "${initrd_start_hi_hex}" "${initrd_start_lo_hex}"
  fdtput -t x "${SMOKE_DTB}" /chosen linux,initrd-end \
    "${initrd_end_hi_hex}" "${initrd_end_lo_hex}"

  # Constrain ISA string to currently implemented extension subset.
  for ((hart = 0; hart < HART_COUNT; hart++)); do
    cpu_node="/cpus/cpu@${hart}"
    if ! fdtget "${SMOKE_DTB}" "${cpu_node}" reg >/dev/null 2>&1; then
      echo "[ERR] DTB missing cpu node: ${cpu_node} (HART_COUNT=${HART_COUNT})" >&2
      exit 1
    fi
    fdtput -t s "${SMOKE_DTB}" "${cpu_node}" riscv,isa "${CPU_ISA}"
  done

  # Keep DT memory size aligned with --memory-mb passed to emulator.
  fdtput -t x "${SMOKE_DTB}" /memory@80000000 reg 0 80000000 \
    "${mem_size_hi_hex}" "${mem_size_lo_hex}"

  # Drop nodes whose MMIO blocks are not implemented in rv32emu yet.
  if fdtget "${SMOKE_DTB}" /soc/rtc@101000 compatible >/dev/null 2>&1; then
    fdtput -r "${SMOKE_DTB}" /soc/rtc@101000
  fi
  if fdtget "${SMOKE_DTB}" /fw-cfg@10100000 compatible >/dev/null 2>&1; then
    fdtput -r "${SMOKE_DTB}" /fw-cfg@10100000
  fi
  if fdtget "${SMOKE_DTB}" /flash@20000000 compatible >/dev/null 2>&1; then
    fdtput -r "${SMOKE_DTB}" /flash@20000000
  fi
  if fdtget "${SMOKE_DTB}" /soc/pci@30000000 compatible >/dev/null 2>&1; then
    fdtput -r "${SMOKE_DTB}" /soc/pci@30000000
  fi
}

if ! command -v timeout >/dev/null 2>&1; then
  echo "[ERR] missing command: timeout" >&2
  exit 1
fi

if ! command -v fdtput >/dev/null 2>&1; then
  echo "[ERR] missing command: fdtput (from device-tree-compiler package)" >&2
  exit 1
fi

if ! command -v fdtget >/dev/null 2>&1; then
  echo "[ERR] missing command: fdtget (from device-tree-compiler package)" >&2
  exit 1
fi

check_positive_int "${HART_COUNT}" "HART_COUNT"
check_positive_int "${DTB_HART_COUNT}" "DTB_HART_COUNT"

if [[ "${DTB_HART_COUNT}" != "${HART_COUNT}" ]]; then
  echo "[WARN] DTB_HART_COUNT (${DTB_HART_COUNT}) != HART_COUNT (${HART_COUNT})"
fi

check_file "${EMU_BIN}"
check_file "${OPENSBI_FW}"
check_file "${KERNEL_IMAGE}"
ensure_dtb_input
check_file "${DTB_IN}"
check_file "${INITRAMFS}"

INITRD_SIZE="$(stat -c '%s' "${INITRAMFS}")"
prepare_dtb

mkdir -p "$(dirname "${LOG_PATH}")"
echo "[INFO] emulator smoke log: ${LOG_PATH}"
echo "[INFO] running ${EMU_BIN} for up to ${TIMEOUT_SEC}s (harts=${HART_COUNT})"

EMU_CMD=(
  "${EMU_BIN}"
  --opensbi "${OPENSBI_FW}"
  --kernel "${KERNEL_IMAGE}"
  --dtb "${SMOKE_DTB}"
  --initrd "${INITRAMFS}"
  --opensbi-load "${OPENSBI_LOAD}"
  --kernel-load "${KERNEL_LOAD}"
  --dtb-load "${DTB_LOAD}"
  --initrd-load "${INITRD_LOAD}"
  --fw-dynamic-info "${FW_DYNAMIC_INFO}"
  --memory-mb "${MEMORY_MB}"
  --hart-count "${HART_COUNT}"
  --max-instr "${MAX_INSTR}"
  --boot-mode s
)

if [[ -n "${EXTRA_EMU_ARGS:-}" ]]; then
  # shellcheck disable=SC2206
  EXTRA_ARGS=( ${EXTRA_EMU_ARGS} )
  EMU_CMD+=( "${EXTRA_ARGS[@]}" )
fi

set +e
timeout "${TIMEOUT_SEC}s" "${EMU_CMD[@]}" >"${LOG_PATH}" 2>&1
EMU_RC=$?
set -e

if [[ "${EMU_RC}" -ne 0 && "${EMU_RC}" -ne 124 && "${EMU_RC}" -ne 137 ]]; then
  echo "[ERR] emulator exited unexpectedly: rc=${EMU_RC}" >&2
  tail -n 120 "${LOG_PATH}" >&2 || true
  exit 1
fi

check_marker "OpenSBI v" "OpenSBI banner"

if [[ "${REQUIRE_LINUX_BANNER}" == "1" ]]; then
  check_marker "Linux version" "Linux banner"
  check_marker "Kernel command line:" "kernel cmdline"
fi

if [[ "${REQUIRE_INIT_MARKER}" == "1" ]]; then
  check_marker "Run /bin/sh as init process|Run /init as init process" "init handoff"
fi

if [[ "${REQUIRE_SECONDARY_HART}" == "1" ]]; then
  check_marker "hart${SECONDARY_HART_ID} state: running=1" "hart${SECONDARY_HART_ID} running"
fi

if grep -Eq "Kernel panic - not syncing|No working init found" "${LOG_PATH}"; then
  if [[ "${ALLOW_INIT_PANIC}" == "1" ]]; then
    echo "[WARN] kernel panic detected after init handoff."
    echo "[WARN] set ALLOW_INIT_PANIC=0 for strict mode."
  else
    echo "[ERR] kernel panic detected during smoke test." >&2
    tail -n 120 "${LOG_PATH}" >&2 || true
    exit 1
  fi
fi

if grep -Eq "error -8" "${LOG_PATH}"; then
  if [[ "${ALLOW_INIT_PANIC}" == "1" ]]; then
    echo "[WARN] executable format error detected (error -8)."
  else
    echo "[ERR] executable format error detected (error -8)." >&2
    tail -n 120 "${LOG_PATH}" >&2 || true
    exit 1
  fi
fi

if grep -Eq "illegal instruction|inst.*fault|page fault" "${LOG_PATH}"; then
  echo "[WARN] possible ISA/MMU gap detected in emulator log."
fi

echo "[OK] emulator smoke test passed"
tail -n 40 "${LOG_PATH}" || true
