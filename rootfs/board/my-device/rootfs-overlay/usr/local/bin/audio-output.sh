#!/bin/sh
# audio-output.sh toggle|speaker|headphones|get
# The GA36-MB can't detect the headphone jack and the speaker amp is fed from the
# headphone output, so "headphones" just turns the speaker off (Speaker Switch).

CARD=0

cur=$(amixer -c "$CARD" cget name='Speaker Switch' 2>/dev/null | grep -o ': values=[a-z]*' | cut -d= -f2)
[ -n "$cur" ] || exit 1
[ "$cur" = on ] && cur=speaker || cur=headphones

case "$1" in
    get)    echo "$cur"; exit 0 ;;
    toggle) [ "$cur" = speaker ] && new=headphones || new=speaker ;;
    speaker|headphones) new=$1 ;;
    *)      echo "usage: $0 toggle|speaker|headphones|get" >&2; exit 2 ;;
esac

[ "$new" = headphones ] && sw=off || sw=on
amixer -q -c "$CARD" cset name='Speaker Switch' "$sw"
