#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${OUT_DIR:-${ROOT_DIR}/out/rootfs}"
ROOTFS_DIR="${OUT_DIR}/rootfs"
INITRAMFS="${OUT_DIR}/initramfs.cpio.gz"

if [[ -n "${BUSYBOX_BIN:-}" ]]; then
  BUSYBOX_BIN="${BUSYBOX_BIN}"
elif [[ -x "${ROOT_DIR}/out/busybox/busybox" ]]; then
  BUSYBOX_BIN="${ROOT_DIR}/out/busybox/busybox"
else
  BUSYBOX_BIN="${ROOT_DIR}/busybox/busybox"
fi

if [[ ! -x "${BUSYBOX_BIN}" ]]; then
  echo "[ERR] busybox binary missing or not executable: ${BUSYBOX_BIN}" >&2
  echo "      run ./scripts/build-busybox.sh or set BUSYBOX_BIN." >&2
  exit 1
fi

mkdir -p "${ROOTFS_DIR}"/{bin,sbin,etc,proc,sys,dev,etc/init.d}
cp -f "${BUSYBOX_BIN}" "${ROOTFS_DIR}/bin/busybox"
chmod +x "${ROOTFS_DIR}/bin/busybox"

# BusyBox applet links
(
  cd "${ROOTFS_DIR}/bin"
  ./busybox --list | while IFS= read -r app; do
    ln -sf busybox "${app}"
  done
)

ln -sf bin/busybox "${ROOTFS_DIR}/init"

cat > "${ROOTFS_DIR}/etc/inittab" <<'INITTAB'
::sysinit:/etc/init.d/rcS
console::respawn:/bin/sh
INITTAB

cat > "${ROOTFS_DIR}/etc/init.d/rcS" <<'RCS'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
echo "[rootfs] boot completed"
RCS
chmod +x "${ROOTFS_DIR}/etc/init.d/rcS"

mkdir -p "${OUT_DIR}"

echo "[INFO] creating initramfs with fakeroot (no host sudo required)"
fakeroot -- bash -c "
  mknod -m 666 '${ROOTFS_DIR}/dev/console' c 5 1
  mknod -m 666 '${ROOTFS_DIR}/dev/null' c 1 3
  cd '${ROOTFS_DIR}'
  find . -print0 | cpio --null -o --format=newc | gzip -9 > '${INITRAMFS}'
"

echo "[OK] initramfs: ${INITRAMFS}"
