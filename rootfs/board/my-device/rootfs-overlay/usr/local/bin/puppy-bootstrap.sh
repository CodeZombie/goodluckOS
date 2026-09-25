#!/bin/sh

# store puppy-boostrap's PID (so it can be killed by poweroff/reboot)
echo $$ > /dev/shm/puppy-bootstrap-pid

if [ -f "/home/player/autolaunch" ]; then
    cmd="$(cat /home/player/autolaunch)"
    sh -c "exec $cmd" &
    echo $! > /dev/shm/puppy-active-process-id
    wait $!
    rm -f /dev/shm/puppy-active-process-id
fi

while true; do
    rm -f /dev/shm/launch

    /usr/bin/puppy

    if [ -f "/dev/shm/launch" ]; then
        cmd="$(cat /dev/shm/launch)"
        if [ -n "$cmd" ]; then
            sh -c "exec $cmd" &
            echo $! > /dev/shm/puppy-active-process-id
            wait $!
            rm -f /dev/shm/puppy-active-process-id
        fi
    fi
done
