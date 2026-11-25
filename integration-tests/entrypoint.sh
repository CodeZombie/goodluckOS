#!/usr/bin/env bash
# Converts rootfs.tar -> ext4 image, then runs pytest (or console)
set -euo pipefail

WORK=/tmp/work
STAGE="$WORK/stage"
export ROOTFS_IMG="$WORK/rootfs.ext4"
FREE_MB="${ROOTFS_FREE_MB:-256}"

rm -rf "$WORK"; mkdir -p "$STAGE"

echo ">> Extracting rootfs.tar"
# Runs as root in the container, so ownership and device nodes are preserved.
tar -xpf /inputs/rootfs.tar -C "$STAGE"

used_mb=$(du -sm "$STAGE" | cut -f1)
size_mb=$(( used_mb + FREE_MB ))

echo ">> Building ext4 image (${size_mb} MB)"
echo 'hvc0::respawn:/sbin/getty -L hvc0 115200 vt100' >> "$STAGE/etc/inittab"
mke2fs -q -t ext4 -L rootfs -E root_owner=0:0 -d "$STAGE" "$ROOTFS_IMG" "${size_mb}m"
rm -rf "$STAGE"

echo ">> Running tests"

QEMU_CMD=(qemu-system-arm -M virt -cpu cortex-a7 -m 512M -nic none
    -kernel /inputs/kernel
    -append "root=/dev/vda rw console=ttyAMA0 earlycon"
    -drive if=none,file="$ROOTFS_IMG",id=disk,format=raw
    -device virtio-blk-device,drive=disk)

case "${1:-}" in
  console-serial)
    # The main console (ttyAMA0). Exit with Ctrl-A then X.
    # Everything prints to your terminal by default.
    exec "${QEMU_CMD[@]}" -nographic
    ;;
  console)
    # The alternate console (hvc0) using the virtio serial device.
    # default serial output gets saved to ./results/ttyAMA0.log
    mkdir -p /tests/results
    rm -f /tmp/cmdchan.sock
    "${QEMU_CMD[@]}" -display none -monitor none \
        -serial file:/tests/results/ttyAMA0.log \
        -device virtio-serial-device \
        -chardev socket,id=cmdchan,path=/tmp/cmdchan.sock,server=on,wait=off \
        -device virtconsole,chardev=cmdchan &
    QEMU_PID=$!
    trap 'kill $QEMU_PID 2>/dev/null' EXIT
    for _ in $(seq 50); do [ -S /tmp/cmdchan.sock ] && break; sleep 0.1; done
    echo ">> Attached to hvc0. Waiting for boot. Press Enter to get a login prompt. Exit with Ctrl-]"
    echo ">> Boot/serial output: results/ttyAMA0.log (tail -f it on the host)"
    socat -,raw,echo=0,escape=0x1d UNIX-CONNECT:/tmp/cmdchan.sock
    exit 0
    ;;
esac

exec pytest "$@"
