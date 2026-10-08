#!/bin/sh
# Mounts the TF-2 card's largest FAT partition at /media/external (udev, 90-external-card.rules)
M=/media/external
L=/run/external-card.lock

# udev runs one copy per partition at once: take turns
n=0
until mkdir "$L" 2>/dev/null; do n=$((n + 1)); [ $n -ge 30 ] && break; sleep 1; done
trap 'rmdir "$L"' EXIT

# Shows or hides the card on the PC (MTP). Only while umtprd runs: started by -cmd, it'd become the daemon
mtp() {
    [ -f /run/umtprd.pid ] && kill -0 "$(cat /run/umtprd.pid)" 2>/dev/null && umtprd "-cmd:$1:External card" >/dev/null 2>&1
}

case "$1" in
    add)
        # already mounted and readable; a mount left from a card that came out isn't
        mountpoint -q "$M" && df "$M" >/dev/null 2>&1 && exit 0
        while mountpoint -q "$M"; do umount -l "$M"; done
        best=""; size=0
        for p in /sys/block/mmcblk1/mmcblk1p*; do
            d=${p##*/}; s=$(cat "$p/size")
            blkid "/dev/$d" | grep -q 'TYPE="vfat"' && [ "$s" -gt "$size" ] && { best=$d; size=$s; }
        done
        [ -n "$best" ] || exit 0
        mkdir -p "$M"
        mount -t vfat -o rw,noatime,uid=1000,gid=1000,umask=022 "/dev/$best" "$M" || exit 0
        # a card without roms/ gets the same system folders as HOME
        if [ ! -d "$M/roms" ]; then
            for d in nes snes gba gbc gb psx genesis sms gg segacd; do mkdir -p "$M/roms/$d/icons"; done
        fi
        mtp mount
        ;;
    remove)
        mountpoint -q "$M" && mtp unmount
        sync
        while mountpoint -q "$M"; do umount "$M" 2>/dev/null || umount -l "$M"; done
        ;;
    mtp)
        # a card mounted before umtprd started (S30usb-gadget)
        mountpoint -q "$M" && mtp mount
        ;;
esac
exit 0
