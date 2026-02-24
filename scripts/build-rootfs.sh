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
  BUSYBOX_BIN=""
fi

if [[ ! -x "${BUSYBOX_BIN}" ]]; then
  echo "[ERR] busybox binary missing or not executable: ${BUSYBOX_BIN}" >&2
  echo "      run ./scripts/build-busybox.sh or set BUSYBOX_BIN." >&2
  exit 1
fi

BUSYBOX_OUT="${BUSYBOX_OUT:-$(dirname "${BUSYBOX_BIN}")}"
BUSYBOX_LINKS="${BUSYBOX_LINKS:-${BUSYBOX_OUT}/busybox.links}"

if [[ ! -f "${BUSYBOX_LINKS}" ]]; then
  echo "[ERR] busybox applet list missing: ${BUSYBOX_LINKS}" >&2
  echo "      run ./scripts/build-busybox.sh first." >&2
  exit 1
fi

rm -rf "${ROOTFS_DIR}"
mkdir -p "${ROOTFS_DIR}"

mkdir -p "${ROOTFS_DIR}/bin"
cp -f "${BUSYBOX_BIN}" "${ROOTFS_DIR}/bin/busybox"
chmod +x "${ROOTFS_DIR}/bin/busybox"

echo "[INFO] creating busybox applet links from ${BUSYBOX_LINKS}"
while IFS= read -r app; do
  [[ -z "${app}" ]] && continue
  app_rel="${app#/}"
  app_path="${ROOTFS_DIR}/${app_rel}"
  mkdir -p "$(dirname "${app_path}")"
  ln -sf /bin/busybox "${app_path}"
done < "${BUSYBOX_LINKS}"

mkdir -p "${ROOTFS_DIR}"/{etc,proc,sys,dev,etc/init.d}

ln -sf bin/busybox "${ROOTFS_DIR}/init"

cat > "${ROOTFS_DIR}/etc/inittab" <<'INITTAB'
::sysinit:/etc/init.d/rcS
::respawn:/bin/cttyhack /bin/sh
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
