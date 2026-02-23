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
SMOKE_ATTEMPTS="${SMOKE_ATTEMPTS:-1}"
MAX_INSTR_STEP="${MAX_INSTR_STEP:-0}"
TIMEOUT_STEP_SEC="${TIMEOUT_STEP_SEC:-0}"

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
  local log_path="$1"
  local pattern="$2"
  local label="$3"
  if ! grep -Eq "${pattern}" "${log_path}"; then
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

check_non_negative_int() {
  local value="$1"
  local label="$2"

  if ! [[ "${value}" =~ ^[0-9]+$ ]]; then
    echo "[ERR] invalid ${label}: ${value}" >&2
    exit 1
  fi
}

attempt_log_path() {
  local attempt="$1"
  if (( SMOKE_ATTEMPTS == 1 )); then
    printf '%s\n' "${LOG_PATH}"
    return 0
  fi
  if [[ "${LOG_PATH}" == *.log ]]; then
    printf '%s.attempt%s.log\n' "${LOG_PATH%.log}" "${attempt}"
    return 0
  fi
  printf '%s.attempt%s\n' "${LOG_PATH}" "${attempt}"
}

print_log_diagnostics() {
  local log_path="$1"
  local key_pattern

  key_pattern="OpenSBI v|Linux version|Kernel command line:|Run /bin/sh as init process|Run /init as init process|SBI HSM extension detected|smp: Bringing up secondary CPUs|CPU[0-9]+: failed to come online|hart[0-9]+ state: running=|rv32emu stop:|Kernel panic - not syncing|No working init found|soft lockup|watchdog: BUG|illegal instruction|inst.*fault|page fault"

  echo "[INFO] key markers from ${log_path}:"
  grep -En "${key_pattern}" "${log_path}" || echo "[INFO] (no key markers found)"
  echo "[INFO] tail -n 80 ${log_path}:"
  tail -n 80 "${log_path}" || true
}

run_emulator_once() {
  local log_path="$1"
  local timeout_sec="$2"
  local max_instr="$3"
  local emu_rc
  local -a emu_cmd

  echo "[INFO] emulator smoke log: ${log_path}"
  echo "[INFO] running ${EMU_BIN} for up to ${timeout_sec}s (harts=${HART_COUNT}, max_instr=${max_instr})"

  emu_cmd=(
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
    --max-instr "${max_instr}"
    --boot-mode s
  )

  if [[ -n "${EXTRA_EMU_ARGS:-}" ]]; then
    # shellcheck disable=SC2206
    EXTRA_ARGS=( ${EXTRA_EMU_ARGS} )
    emu_cmd+=( "${EXTRA_ARGS[@]}" )
  fi

  set +e
  timeout "${timeout_sec}s" "${emu_cmd[@]}" >"${log_path}" 2>&1
  emu_rc=$?
  set -e

  if [[ "${emu_rc}" -ne 0 && "${emu_rc}" -ne 124 && "${emu_rc}" -ne 137 ]]; then
    echo "[ERR] emulator exited unexpectedly: rc=${emu_rc}" >&2
    return 1
  fi
  return 0
}

validate_smoke_markers() {
  local log_path="$1"
  local ok=0

  check_marker "${log_path}" "OpenSBI v" "OpenSBI banner" || ok=1

  if [[ "${REQUIRE_LINUX_BANNER}" == "1" ]]; then
    check_marker "${log_path}" "Linux version" "Linux banner" || ok=1
    check_marker "${log_path}" "Kernel command line:" "kernel cmdline" || ok=1
  fi

  if [[ "${REQUIRE_INIT_MARKER}" == "1" ]]; then
    check_marker "${log_path}" "Run /bin/sh as init process|Run /init as init process" "init handoff" || ok=1
  fi

  if [[ "${REQUIRE_SECONDARY_HART}" == "1" ]]; then
    check_marker "${log_path}" "hart${SECONDARY_HART_ID} state: running=1" "hart${SECONDARY_HART_ID} running" || ok=1
  fi

  if grep -Eq "Kernel panic - not syncing|No working init found" "${log_path}"; then
    if [[ "${ALLOW_INIT_PANIC}" == "1" ]]; then
      echo "[WARN] kernel panic detected after init handoff."
      echo "[WARN] set ALLOW_INIT_PANIC=0 for strict mode."
    else
      echo "[ERR] kernel panic detected during smoke test." >&2
      ok=1
    fi
  fi

  if grep -Eq "error -8" "${log_path}"; then
    if [[ "${ALLOW_INIT_PANIC}" == "1" ]]; then
      echo "[WARN] executable format error detected (error -8)."
    else
      echo "[ERR] executable format error detected (error -8)." >&2
      ok=1
    fi
  fi

  if grep -Eq "illegal instruction|inst.*fault|page fault" "${log_path}"; then
    echo "[WARN] possible ISA/MMU gap detected in emulator log."
  fi

  (( ok == 0 ))
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
check_positive_int "${TIMEOUT_SEC}" "TIMEOUT_SEC"
check_positive_int "${MAX_INSTR}" "MAX_INSTR"
check_positive_int "${SMOKE_ATTEMPTS}" "SMOKE_ATTEMPTS"
check_non_negative_int "${MAX_INSTR_STEP}" "MAX_INSTR_STEP"
check_non_negative_int "${TIMEOUT_STEP_SEC}" "TIMEOUT_STEP_SEC"

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
PASS_ATTEMPT=0
for ((attempt = 1; attempt <= SMOKE_ATTEMPTS; attempt++)); do
  ATTEMPT_LOG_PATH="$(attempt_log_path "${attempt}")"
  ATTEMPT_TIMEOUT_SEC=$((TIMEOUT_SEC + (attempt - 1) * TIMEOUT_STEP_SEC))
  ATTEMPT_MAX_INSTR=$((MAX_INSTR + (attempt - 1) * MAX_INSTR_STEP))

  if (( SMOKE_ATTEMPTS > 1 )); then
    echo "[INFO] smoke attempt ${attempt}/${SMOKE_ATTEMPTS}"
  fi

  if ! run_emulator_once "${ATTEMPT_LOG_PATH}" "${ATTEMPT_TIMEOUT_SEC}" "${ATTEMPT_MAX_INSTR}"; then
    print_log_diagnostics "${ATTEMPT_LOG_PATH}"
    if (( attempt < SMOKE_ATTEMPTS )); then
      echo "[WARN] smoke attempt ${attempt} failed before marker checks; retrying."
      continue
    fi
    echo "[ERR] emulator smoke test failed after ${SMOKE_ATTEMPTS} attempt(s)." >&2
    exit 1
  fi

  if validate_smoke_markers "${ATTEMPT_LOG_PATH}"; then
    PASS_ATTEMPT="${attempt}"
    if [[ "${ATTEMPT_LOG_PATH}" != "${LOG_PATH}" ]]; then
      cp "${ATTEMPT_LOG_PATH}" "${LOG_PATH}"
    fi
    break
  fi

  print_log_diagnostics "${ATTEMPT_LOG_PATH}"
  if (( attempt < SMOKE_ATTEMPTS )); then
    echo "[WARN] smoke attempt ${attempt} failed marker checks; retrying."
  fi
done

if (( PASS_ATTEMPT == 0 )); then
  echo "[ERR] emulator smoke test failed after ${SMOKE_ATTEMPTS} attempt(s)." >&2
  exit 1
fi

if (( SMOKE_ATTEMPTS > 1 )); then
  echo "[OK] emulator smoke test passed on attempt ${PASS_ATTEMPT}/${SMOKE_ATTEMPTS}"
else
  echo "[OK] emulator smoke test passed"
fi
tail -n 40 "${LOG_PATH}" || true
