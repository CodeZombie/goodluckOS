#!/bin/sh

while true; do
    rm -f /tmp/launch

    /usr/bin/puppy

    if [ -f /tmp/launch ]; then
        cmd="$(cat /tmp/launch)"
        if [ -n "$cmd" ]; then
            sh -c "exec $cmd" &
            echo $! > /tmp/puppy-active-process-id
            wait $!
            rm -f /tmp/puppy-active-process-id
        fi
    else
        sleep 1
    fi
done
