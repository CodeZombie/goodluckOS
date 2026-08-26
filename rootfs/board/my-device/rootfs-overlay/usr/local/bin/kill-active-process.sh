#!/bin/sh
PIDFILE=/tmp/puppy-active-process-id
[ -f "$PIDFILE" ] || exit 0
pid=$(cat "$PIDFILE")
case "$pid" in
    ''|*[!0-9]*) exit 0 ;;   # not a valid number, bail
esac
kill -9 "$pid" 2>/dev/null
