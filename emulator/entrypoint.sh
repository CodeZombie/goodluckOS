#!/usr/bin/env bash
# Converts rootfs.tar -> ext4 image, then runs pytest (or console)
set -euo pipefail

WORK=/tmp/work
STAGE="$WORK/stage"
export ROOTFS_IMG="$WORK/rootfs.ext4"
FREE_MB="${ROOTFS_FREE_MB:-256}"

rm -rf "$WORK"; mkdir -p "$STAGE"

rm -rf /srv; mkdir -p /srv
cp -a /opt/web/. /srv/
cp -a /usr/share/novnc /srv/novnc
printf '{"width":%s,"height":%s}\n' "$SCREEN_W" "$SCREEN_H" > /srv/screen.json

echo ">> Extracting rootfs.tar"
# Runs as root in the container, so ownership and device nodes are preserved.
tar -xpf /inputs/rootfs.tar -C "$STAGE"

used_mb=$(du -sm "$STAGE" | cut -f1)
size_mb=$(( used_mb + FREE_MB ))

echo ">> Building ext4 image (${size_mb} MB)"
 #echo 'hvc0::respawn:/sbin/getty -L hvc0 115200 vt100' >> "$STAGE/etc/inittab"
mke2fs -q -t ext4 -L rootfs -E root_owner=0:0 -d "$STAGE" "$ROOTFS_IMG" "${size_mb}m"
rm -rf "$STAGE"

echo ">> Starting emulator..."


QEMU=(exec qemu-system-arm -M virt,highmem=off -cpu max -m 512M -smp 4 -nic none
    -kernel /inputs/kernel
    -append "root=/dev/vda rw console=ttyAMA0 earlycon"
    -drive if=none,file="$ROOTFS_IMG",id=disk,format=raw
    -device virtio-blk-pci,drive=disk
    -device virtio-gpu-gl-pci,xres="$SCREEN_W",yres="$SCREEN_H"
    -device virtio-keyboard-pci
    -display egl-headless,rendernode=/dev/dri/renderD128
    -chardev "socket,id=ser0,host=0.0.0.0,port=5556,server=on,wait=off,logfile=/dev/stdout,logappend=on"
    -serial chardev:ser0
    -serial mon:stdio
    -vnc 0.0.0.0:0
)

cat <<BANNER

  goodluckOS Emulator
    Screen + buttons : http://localhost:${WEB_PORT:-8080}
    Serial console   : nc localhost ${SERIAL_PORT:-5556}

BANNER

"${QEMU[@]}" </dev/null &
QPID=$!
websockify --web /srv 8080 127.0.0.1:5900 >/dev/null 2>&1 &
WPID=$!

cleanup() { kill "$QPID" "$WPID" 2>/dev/null || true; }
trap cleanup TERM INT EXIT
set +e
wait -n "$QPID" "$WPID"
CODE=$?
echo "[emulator] a process exited (code $CODE) - shutting down"
exit "$CODE"
