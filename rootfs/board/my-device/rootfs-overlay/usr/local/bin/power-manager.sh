#!/bin/sh
FIFO=/run/power-request
[ -p "$FIFO" ] || mkfifo -m 660 "$FIFO"
chgrp player "$FIFO" 2>/dev/null   # let unprivileged 'player' write requests

exec 3<>"$FIFO"

do_shutdown() {
    action="$1"
    if [ "$action" = "reboot" ]; then
        /sbin/reboot
    else
        /sbin/poweroff
    fi
}

while true; do
    read -r cmd <&3
    [ -n "$cmd" ] || continue
    case "$cmd" in
        poweroff|reboot)
            do_shutdown "$cmd"
            ;;
    esac
done
