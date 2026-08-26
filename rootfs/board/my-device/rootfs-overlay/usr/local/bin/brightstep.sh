#!/bin/sh
BL=$(echo /sys/class/backlight/*)
max=$(cat "$BL/max_brightness")
cur=$(cat "$BL/brightness")
step=$((max / 10)); [ "$step" -lt 1 ] && step=1

case "$1" in
    up)   new=$((cur + step)); [ "$new" -gt "$max" ] && new=$max ;;
    down) new=$((cur - step)); [ "$new" -lt 1 ] && new=1 ;;
esac

echo "$new" > "$BL/brightness"
