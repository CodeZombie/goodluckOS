#!/bin/sh
CARD=0
CONTROL=Headphone
STEP=5

cur=$(amixer -c "$CARD" sget "$CONTROL" | grep -m1 -oE '[0-9]+%' | tr -d '%')

case "$1" in
    up)   new=$((cur + STEP)); [ "$new" -gt 100 ] && new=100 ;;
    down) new=$((cur - STEP)); [ "$new" -lt 0 ] && new=0 ;;
esac

if [ "$new" -eq 0 ]; then
    amixer -q -c "$CARD" sset "$CONTROL" 0% mute
else
    amixer -q -c "$CARD" sset "$CONTROL" "${new}%" unmute
fi
