#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCES_DIR="${SOURCES_DIR:-${ROOT_DIR}/out/sources}"

FETCH_LINUX="${FETCH_LINUX:-1}"
FETCH_OPENSBI="${FETCH_OPENSBI:-1}"
FETCH_BUSYBOX="${FETCH_BUSYBOX:-1}"
FETCH_TOOLCHAIN="${FETCH_TOOLCHAIN:-0}"

UPDATE_EXISTING="${UPDATE_EXISTING:-1}"
SHALLOW_CLONE="${SHALLOW_CLONE:-1}"

LINUX_REPO="${LINUX_REPO:-https://github.com/torvalds/linux.git}"
LINUX_REF="${LINUX_REF:-v6.6}"
LINUX_DIR="${LINUX_DIR:-${SOURCES_DIR}/linux}"

OPENSBI_REPO="${OPENSBI_REPO:-https://github.com/riscv-software-src/opensbi.git}"
OPENSBI_REF="${OPENSBI_REF:-v1.5.1}"
OPENSBI_DIR="${OPENSBI_DIR:-${SOURCES_DIR}/opensbi}"

BUSYBOX_REPO="${BUSYBOX_REPO:-https://github.com/mirror/busybox.git}"
BUSYBOX_REF="${BUSYBOX_REF:-1_36_1}"
BUSYBOX_DIR="${BUSYBOX_DIR:-${SOURCES_DIR}/busybox}"

TOOLCHAIN_REPO="${TOOLCHAIN_REPO:-https://github.com/riscv-collab/riscv-gnu-toolchain.git}"
TOOLCHAIN_REF="${TOOLCHAIN_REF:-master}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-${SOURCES_DIR}/riscv-gnu-toolchain}"

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "[ERR] missing command: $1" >&2
    exit 1
  fi
}

checkout_ref() {
  local ref="$1"
  local target=""

  if git rev-parse -q --verify "refs/tags/${ref}" >/dev/null; then
    target="refs/tags/${ref}"
  elif git rev-parse -q --verify "refs/remotes/origin/${ref}" >/dev/null; then
    target="refs/remotes/origin/${ref}"
  elif git rev-parse -q --verify "${ref}^{commit}" >/dev/null; then
    target="${ref}"
  else
    git fetch --force origin "${ref}" >/dev/null 2>&1 || true
    if git rev-parse -q --verify "FETCH_HEAD^{commit}" >/dev/null; then
      target="FETCH_HEAD"
    else
      echo "[ERR] cannot resolve ref: ${ref}" >&2
      exit 1
    fi
  fi

  git -c advice.detachedHead=false checkout --force --detach "${target}" >/dev/null
}

sync_repo() {
  local name="$1"
  local repo_url="$2"
  local repo_dir="$3"
  local repo_ref="$4"

  if [[ -e "${repo_dir}" && ! -d "${repo_dir}/.git" ]]; then
    echo "[ERR] target exists but is not a git repo: ${repo_dir}" >&2
    exit 1
  fi

  if [[ ! -d "${repo_dir}/.git" ]]; then
    echo "[INFO] cloning ${name} (${repo_ref})"
    if [[ "${SHALLOW_CLONE}" == "1" ]]; then
      git clone --depth 1 --branch "${repo_ref}" "${repo_url}" "${repo_dir}"
    else
      git clone "${repo_url}" "${repo_dir}"
    fi
  elif [[ "${UPDATE_EXISTING}" == "1" ]]; then
    echo "[INFO] updating ${name} (${repo_ref})"
    if [[ "${SHALLOW_CLONE}" == "1" ]]; then
      (
        cd "${repo_dir}"
        git fetch --depth 1 --force origin "${repo_ref}" >/dev/null 2>&1 || true
        git fetch --depth 1 --force origin "refs/tags/${repo_ref}:refs/tags/${repo_ref}" >/dev/null 2>&1 || true
      )
    else
      (
        cd "${repo_dir}"
        git fetch --force --tags origin
      )
    fi
  else
    echo "[INFO] keeping existing ${name} checkout"
  fi

  (
    cd "${repo_dir}"
    checkout_ref "${repo_ref}"
    echo "[OK] ${name}: $(git rev-parse --short HEAD) @ ${repo_dir}"
  )
}

require_cmd git
mkdir -p "${SOURCES_DIR}"

if [[ "${FETCH_LINUX}" == "1" ]]; then
  sync_repo "linux" "${LINUX_REPO}" "${LINUX_DIR}" "${LINUX_REF}"
fi

if [[ "${FETCH_OPENSBI}" == "1" ]]; then
  sync_repo "opensbi" "${OPENSBI_REPO}" "${OPENSBI_DIR}" "${OPENSBI_REF}"
fi

if [[ "${FETCH_BUSYBOX}" == "1" ]]; then
  sync_repo "busybox" "${BUSYBOX_REPO}" "${BUSYBOX_DIR}" "${BUSYBOX_REF}"
fi

if [[ "${FETCH_TOOLCHAIN}" == "1" ]]; then
  sync_repo "riscv-gnu-toolchain" "${TOOLCHAIN_REPO}" "${TOOLCHAIN_DIR}" "${TOOLCHAIN_REF}"
fi

echo "[OK] source sync complete"
