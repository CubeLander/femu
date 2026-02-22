#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

TOOLCHAIN_ROOT="${TOOLCHAIN_ROOT:-${ROOT_DIR}/opt/toolchains}"
DOWNLOAD_DIR="${DOWNLOAD_DIR:-${ROOT_DIR}/out/downloads}"

RV32_TC_FAMILY="${RV32_TC_FAMILY:-riscv32-ilp32d}"
RV32_TC_LIBC="${RV32_TC_LIBC:-glibc}"       # glibc|musl|uclibc
RV32_TC_CHANNEL="${RV32_TC_CHANNEL:-stable}" # stable|bleeding-edge
RV32_TC_VERSION="${RV32_TC_VERSION:-2025.08-1}"

TC_BASENAME="${RV32_TC_FAMILY}--${RV32_TC_LIBC}--${RV32_TC_CHANNEL}-${RV32_TC_VERSION}"
TC_TARBALL="${TC_BASENAME}.tar.xz"
TC_BASE_URL="https://toolchains.bootlin.com/downloads/releases/toolchains/${RV32_TC_FAMILY}/tarballs"
TC_URL="${TC_BASE_URL}/${TC_TARBALL}"
SHA_URL="${TC_BASE_URL}/${TC_BASENAME}.sha256"

TC_ARCHIVE_PATH="${DOWNLOAD_DIR}/${TC_TARBALL}"
TC_SHA_PATH="${DOWNLOAD_DIR}/${TC_BASENAME}.sha256"
TC_INSTALL_DIR="${TOOLCHAIN_ROOT}/${TC_BASENAME}"
TC_PREFIX="${TC_INSTALL_DIR}/bin/riscv32-linux-"

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "[ERR] missing command: $1" >&2
    exit 1
  fi
}

check_toolchain() {
  if [[ ! -x "${TC_PREFIX}gcc" ]]; then
    return 1
  fi

  local tmpdir
  tmpdir="$(mktemp -d)"
  cat > "${tmpdir}/probe.c" <<'EOF'
#include <stdio.h>
int main(void) { puts("rv32-ok"); return 0; }
EOF

  if ! "${TC_PREFIX}gcc" -static -o "${tmpdir}/probe" "${tmpdir}/probe.c" >/dev/null 2>&1; then
    rm -rf "${tmpdir}"
    return 1
  fi

  if ! file "${tmpdir}/probe" | grep -q "ELF 32-bit"; then
    rm -rf "${tmpdir}"
    return 1
  fi

  rm -rf "${tmpdir}"
  return 0
}

require_cmd curl
require_cmd tar
require_cmd sha256sum
require_cmd file

mkdir -p "${TOOLCHAIN_ROOT}" "${DOWNLOAD_DIR}"

if check_toolchain; then
  echo "[OK] rv32 toolchain already installed: ${TC_INSTALL_DIR}"
  echo "[HINT] export RV32_CROSS_COMPILE=${TC_PREFIX}"
  exit 0
fi

echo "[INFO] downloading: ${TC_URL}"
curl -fL --retry 3 --retry-delay 2 -o "${TC_ARCHIVE_PATH}" "${TC_URL}"

echo "[INFO] downloading checksum: ${SHA_URL}"
curl -fL --retry 3 --retry-delay 2 -o "${TC_SHA_PATH}" "${SHA_URL}"

echo "[INFO] verifying checksum"
(
  cd "${DOWNLOAD_DIR}"
  sha256sum -c "$(basename "${TC_SHA_PATH}")"
)

if [[ -d "${TC_INSTALL_DIR}" ]]; then
  echo "[INFO] removing broken previous install: ${TC_INSTALL_DIR}"
  rm -rf "${TC_INSTALL_DIR}"
fi

echo "[INFO] extracting toolchain to: ${TOOLCHAIN_ROOT}"
tar -xJf "${TC_ARCHIVE_PATH}" -C "${TOOLCHAIN_ROOT}"

if ! check_toolchain; then
  echo "[ERR] installed toolchain failed rv32 probe: ${TC_INSTALL_DIR}" >&2
  exit 1
fi

echo "[OK] rv32 toolchain installed: ${TC_INSTALL_DIR}"
echo "[HINT] export PATH=${TC_INSTALL_DIR}/bin:\$PATH"
echo "[HINT] export RV32_CROSS_COMPILE=${TC_PREFIX}"
