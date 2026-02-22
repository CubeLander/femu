#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

LOG_PATH="${LOG_PATH:-${ROOT_DIR}/out/smoke/emulator-smoke-interactive.log}"
TIMEOUT_SEC="${TIMEOUT_SEC:-120}"
MAX_INSTR="${MAX_INSTR:-1800000000}"
INJECT_DELAY_SEC="${INJECT_DELAY_SEC:-55}"
INJECT_INTERVAL_SEC="${INJECT_INTERVAL_SEC:-3}"
INJECT_COUNT="${INJECT_COUNT:-6}"
INTERACTIVE_MARKER="${INTERACTIVE_MARKER:-__RV32EMU_INTERACTIVE_OK__}"

EMU_EXTRA_ARGS="--interactive"
if [[ -n "${EXTRA_EMU_ARGS:-}" ]]; then
  EMU_EXTRA_ARGS="${EXTRA_EMU_ARGS} ${EMU_EXTRA_ARGS}"
fi

mkdir -p "$(dirname "${LOG_PATH}")"

echo "[INFO] interactive smoke log: ${LOG_PATH}"
echo "[INFO] will inject ${INJECT_COUNT} probe command(s) after ${INJECT_DELAY_SEC}s"

{
  sleep "${INJECT_DELAY_SEC}"
  i=0
  while (( i < INJECT_COUNT )); do
    printf 'echo %s\n' "${INTERACTIVE_MARKER}"
    sleep "${INJECT_INTERVAL_SEC}"
    i=$((i + 1))
  done
} | LOG_PATH="${LOG_PATH}" \
    TIMEOUT_SEC="${TIMEOUT_SEC}" \
    MAX_INSTR="${MAX_INSTR}" \
    ALLOW_INIT_PANIC=0 \
    EXTRA_EMU_ARGS="${EMU_EXTRA_ARGS}" \
    "${ROOT_DIR}/scripts/smoke-emulator.sh"

if ! grep -q "${INTERACTIVE_MARKER}" "${LOG_PATH}"; then
  echo "[ERR] interactive marker missing: ${INTERACTIVE_MARKER}" >&2
  tail -n 120 "${LOG_PATH}" >&2 || true
  exit 1
fi

if grep -Eq "can't access tty|job control turned off" "${LOG_PATH}"; then
  echo "[ERR] shell still has no controlling tty" >&2
  tail -n 120 "${LOG_PATH}" >&2 || true
  exit 1
fi

echo "[OK] emulator interactive smoke test passed"
tail -n 60 "${LOG_PATH}" || true
