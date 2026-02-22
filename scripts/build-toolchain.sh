#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

TOOLCHAIN_MODE="${TOOLCHAIN_MODE:-auto}" # auto|apt|source
JOBS="${JOBS:-$(nproc)}"

TOOLCHAIN_REPO="${TOOLCHAIN_REPO:-https://github.com/riscv-collab/riscv-gnu-toolchain.git}"
TOOLCHAIN_REF="${TOOLCHAIN_REF:-master}"
TOOLCHAIN_SRC="${TOOLCHAIN_SRC:-${ROOT_DIR}/riscv-gnu-toolchain}"
TOOLCHAIN_PREFIX="${TOOLCHAIN_PREFIX:-${ROOT_DIR}/opt/riscv}"
TOOLCHAIN_TARGET="${TOOLCHAIN_TARGET:-linux}" # linux|newlib
TOOLCHAIN_ARCH="${TOOLCHAIN_ARCH:-rv32imac}"
TOOLCHAIN_ABI="${TOOLCHAIN_ABI:-ilp32}"
UPDATE_EXISTING="${UPDATE_EXISTING:-1}"
SHALLOW_CLONE="${SHALLOW_CLONE:-1}"

detect_compiler_cmd() {
  if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    command -v riscv64-linux-gnu-gcc
    return 0
  fi
  if command -v riscv64-unknown-linux-gnu-gcc >/dev/null 2>&1; then
    command -v riscv64-unknown-linux-gnu-gcc
    return 0
  fi
  return 1
}

have_cross_compiler() {
  detect_compiler_cmd >/dev/null 2>&1
}

cross_toolchain_ready() {
  local cc=""
  local tmpdir=""

  if ! cc="$(detect_compiler_cmd)"; then
    return 1
  fi

  tmpdir="$(mktemp -d)"
  cat > "${tmpdir}/probe.c" <<'EOF'
#include <stdio.h>
int main(void) { puts("ok"); return 0; }
EOF

  if "${cc}" -static -o "${tmpdir}/probe" "${tmpdir}/probe.c" >/dev/null 2>&1; then
    rm -rf "${tmpdir}"
    return 0
  fi

  rm -rf "${tmpdir}"
  return 1
}

print_detected_compiler() {
  if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    echo "[OK] cross compiler: $(command -v riscv64-linux-gnu-gcc)"
    return 0
  fi
  if command -v riscv64-unknown-linux-gnu-gcc >/dev/null 2>&1; then
    echo "[OK] cross compiler: $(command -v riscv64-unknown-linux-gnu-gcc)"
    return 0
  fi
  return 1
}

apt_install_toolchain() {
  local apt_cmd=""

  if [[ "$(id -u)" == "0" ]]; then
    apt_cmd="apt-get"
  elif command -v sudo >/dev/null 2>&1; then
    apt_cmd="sudo apt-get"
  else
    echo "[ERR] apt mode requires root or sudo." >&2
    exit 1
  fi

  echo "[INFO] installing distro riscv toolchain via apt"
  DEBIAN_FRONTEND=noninteractive ${apt_cmd} update
  DEBIAN_FRONTEND=noninteractive ${apt_cmd} install -y \
    gcc-riscv64-linux-gnu g++-riscv64-linux-gnu \
    binutils-riscv64-linux-gnu libc6-dev-riscv64-cross
}

build_source_toolchain() {
  if [[ ! -d "${TOOLCHAIN_SRC}/.git" ]]; then
    echo "[INFO] cloning riscv-gnu-toolchain (${TOOLCHAIN_REF})"
    if [[ "${SHALLOW_CLONE}" == "1" ]]; then
      git clone --depth 1 --branch "${TOOLCHAIN_REF}" "${TOOLCHAIN_REPO}" "${TOOLCHAIN_SRC}"
    else
      git clone "${TOOLCHAIN_REPO}" "${TOOLCHAIN_SRC}"
    fi
  elif [[ "${UPDATE_EXISTING}" == "1" ]]; then
    echo "[INFO] updating riscv-gnu-toolchain (${TOOLCHAIN_REF})"
    (
      cd "${TOOLCHAIN_SRC}"
      if [[ "${SHALLOW_CLONE}" == "1" ]]; then
        git fetch --depth 1 --force origin "${TOOLCHAIN_REF}" >/dev/null 2>&1 || true
      else
        git fetch --force --tags origin
      fi
    )
  fi

  (
    cd "${TOOLCHAIN_SRC}"
    git -c advice.detachedHead=false checkout --force --detach "${TOOLCHAIN_REF}" >/dev/null 2>&1 || true
    git submodule update --init --recursive
  )

  mkdir -p "${TOOLCHAIN_PREFIX}"
  (
    cd "${TOOLCHAIN_SRC}"
    ./configure --prefix="${TOOLCHAIN_PREFIX}" --with-arch="${TOOLCHAIN_ARCH}" --with-abi="${TOOLCHAIN_ABI}"

    if [[ "${TOOLCHAIN_TARGET}" == "linux" ]]; then
      make linux -j"${JOBS}"
    elif [[ "${TOOLCHAIN_TARGET}" == "newlib" ]]; then
      make -j"${JOBS}"
    else
      echo "[ERR] invalid TOOLCHAIN_TARGET: ${TOOLCHAIN_TARGET}" >&2
      exit 1
    fi
  )

  echo "[OK] source toolchain installed under: ${TOOLCHAIN_PREFIX}"
  echo "[HINT] export PATH=${TOOLCHAIN_PREFIX}/bin:\$PATH"
}

if cross_toolchain_ready; then
  print_detected_compiler
  exit 0
fi

case "${TOOLCHAIN_MODE}" in
  auto)
    if command -v apt-get >/dev/null 2>&1; then
      apt_install_toolchain
    else
      build_source_toolchain
    fi
    ;;
  apt)
    apt_install_toolchain
    ;;
  source)
    build_source_toolchain
    ;;
  *)
    echo "[ERR] invalid TOOLCHAIN_MODE: ${TOOLCHAIN_MODE}" >&2
    echo "      expected: auto | apt | source" >&2
    exit 1
    ;;
esac

if ! cross_toolchain_ready; then
  echo "[ERR] cross toolchain is still unusable after setup." >&2
  echo "      a static test compile failed." >&2
  exit 1
fi

print_detected_compiler
