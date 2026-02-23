#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_TAG="${IMAGE_TAG:-ics2019-dev:latest}"
DOC_WSL="${ROOT_DIR}/docs/migration/wsl2-docker-setup.md"
DOC_PROXY="${ROOT_DIR}/docs/migration/network-proxy.md"
DOCKERFILE_PATH="${ROOT_DIR}/docker/Dockerfile.dev"
IMAGE_HASH_LABEL="org.femu.dev.dockerfile-sha256"

hash_file_sha256() {
  local file="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "${file}" | awk '{print $1}'
    return 0
  fi
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "${file}" | awk '{print $1}'
    return 0
  fi

  return 1
}

image_exists() {
  docker image inspect "${IMAGE_TAG}" >/dev/null 2>&1
}

image_dockerfile_hash() {
  docker image inspect \
    --format "{{ index .Config.Labels \"${IMAGE_HASH_LABEL}\" }}" \
    "${IMAGE_TAG}" 2>/dev/null || true
}

is_wsl() {
  grep -qiE "(microsoft|wsl)" /proc/sys/kernel/osrelease 2>/dev/null
}

setup_proxy_env() {
  # Optional helper for users behind GFW.
  # Set ENABLE_CLASH_PROXY=1 to auto-export proxy envs if they are unset.
  local enable="${ENABLE_CLASH_PROXY:-0}"
  if [[ "${enable}" != "1" ]]; then
    return 0
  fi

  local clash_port="${CLASH_PROXY_PORT:-7890}"
  local proxy_url="${CLASH_PROXY_URL:-http://host.docker.internal:${clash_port}}"

  : "${HTTP_PROXY:=${proxy_url}}"
  : "${HTTPS_PROXY:=${proxy_url}}"
  : "${ALL_PROXY:=${proxy_url}}"
  : "${NO_PROXY:=localhost,127.0.0.1,::1,host.docker.internal}"

  export HTTP_PROXY HTTPS_PROXY ALL_PROXY NO_PROXY
  export http_proxy="${HTTP_PROXY}" https_proxy="${HTTPS_PROXY}" all_proxy="${ALL_PROXY}" no_proxy="${NO_PROXY}"

  echo "[INFO] clash proxy enabled for docker build/run"
  echo "[INFO] HTTP_PROXY=${HTTP_PROXY}"
}

collect_proxy_args() {
  BUILD_PROXY_ARGS=()
  RUN_PROXY_ENVS=()

  local vars=(
    HTTP_PROXY HTTPS_PROXY ALL_PROXY NO_PROXY
    http_proxy https_proxy all_proxy no_proxy
  )
  local v
  for v in "${vars[@]}"; do
    if [[ -n "${!v:-}" ]]; then
      BUILD_PROXY_ARGS+=(--build-arg "${v}=${!v}")
      RUN_PROXY_ENVS+=(-e "${v}=${!v}")
    fi
  done
}

if ! command -v docker >/dev/null 2>&1; then
  echo "[ERR] docker command not found in current shell." >&2
  if is_wsl; then
    cat >&2 <<EOF
[HINT] You are running inside WSL2.
       This script expects Docker Desktop with WSL integration enabled.

Quick fix:
  1) Start Docker Desktop on Windows.
  2) Docker Desktop -> Settings -> Resources -> WSL Integration.
  3) Enable integration for this distro.
  4) In PowerShell: wsl --shutdown
  5) Reopen this distro and run: docker version

Detailed guide:
  ${DOC_WSL}
Proxy guide (GFW):
  ${DOC_PROXY}
EOF
  else
    echo "[HINT] Install Docker (or adapt this script for podman)." >&2
  fi
  exit 1
fi

if ! docker info >/dev/null 2>&1; then
  echo "[ERR] docker exists, but daemon connection failed." >&2
  if is_wsl; then
    cat >&2 <<EOF
[HINT] In WSL2 this usually means Docker Desktop is not running
       or WSL integration is disabled for this distro.
See:
  ${DOC_WSL}
EOF
  fi
  exit 1
fi

setup_proxy_env
collect_proxy_args

should_build=0
dockerfile_hash=""
rebuild_reason=""
force_rebuild="${FORCE_REBUILD:-0}"
hash_available=0

if dockerfile_hash="$(hash_file_sha256 "${DOCKERFILE_PATH}")"; then
  hash_available=1
else
  echo "[WARN] sha256sum/shasum not found, fallback to existing image without hash check."
fi

if [[ "${force_rebuild}" == "1" ]]; then
  should_build=1
  rebuild_reason="FORCE_REBUILD=1"
elif ! image_exists; then
  should_build=1
  rebuild_reason="image not found"
else
  if [[ "${hash_available}" == "1" ]]; then
    current_image_hash="$(image_dockerfile_hash)"
    if [[ -z "${current_image_hash}" ]]; then
      should_build=1
      rebuild_reason="image hash label missing"
    elif [[ "${current_image_hash}" != "${dockerfile_hash}" ]]; then
      should_build=1
      rebuild_reason="Dockerfile changed"
    fi
  fi
fi

if [[ "${should_build}" == "1" ]]; then
  if [[ -z "${dockerfile_hash}" ]]; then
    dockerfile_hash="unknown"
  fi
  echo "[INFO] building dev image: ${IMAGE_TAG} (${rebuild_reason})"
  docker build "${BUILD_PROXY_ARGS[@]}" \
    --label "${IMAGE_HASH_LABEL}=${dockerfile_hash}" \
    -f "${DOCKERFILE_PATH}" \
    -t "${IMAGE_TAG}" \
    "${ROOT_DIR}/docker"
else
  echo "[INFO] reuse cached dev image: ${IMAGE_TAG}"
  echo "[INFO] set FORCE_REBUILD=1 to rebuild manually"
fi

echo "[INFO] entering dev shell"
docker run --rm -it \
  "${RUN_PROXY_ENVS[@]}" \
  -v "${ROOT_DIR}:/workspace" \
  -w /workspace \
  "${IMAGE_TAG}" /bin/bash
