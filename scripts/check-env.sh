#!/usr/bin/env bash
set -euo pipefail

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "[ERR] missing command: $1" >&2
    return 1
  fi
  echo "[OK] $1 -> $(command -v "$1")"
}

require_cmd make
require_cmd gcc
require_cmd qemu-system-riscv32
require_cmd dtc
require_cmd cpio

if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
  echo "[OK] cross compiler: riscv64-linux-gnu-gcc"
elif command -v riscv64-unknown-linux-gnu-gcc >/dev/null 2>&1; then
  echo "[OK] cross compiler: riscv64-unknown-linux-gnu-gcc"
else
  echo "[WARN] riscv cross compiler not found (run ./scripts/build-toolchain.sh)."
fi

if command -v riscv32-linux-gcc >/dev/null 2>&1 || command -v riscv32-buildroot-linux-gnu-gcc >/dev/null 2>&1; then
  echo "[OK] rv32 userspace compiler detected in PATH"
elif [[ -x "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/opt/toolchains/riscv32-ilp32d--glibc--stable-2025.08-1/bin/riscv32-linux-gcc" ]]; then
  echo "[OK] rv32 userspace compiler detected under opt/toolchains"
else
  echo "[WARN] rv32 userspace compiler not found (run ./scripts/install-rv32-toolchain.sh)."
fi
