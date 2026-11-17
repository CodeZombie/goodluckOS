#!/bin/sh
# Power button: turns the screen off (sleep) or back on (wake).
# Sleep also pauses the launcher and the running app (cgroup freezer), so the CPU and GPU go
# idle and the game waits instead of playing on unseen; holds the gamepad, so presses in a
# pocket don't reach the app; and turns the speaker amp off. Waking undoes all three.
BL=/sys/class/backlight/backlight/brightness
FREEZE=/sys/fs/cgroup/appd/cgroup.freeze
SLEEP=/tmp/sleep-state        # what to undo on wake: pid of the gamepad grab, speaker on/off

current_brightness=$(cat "$BL")

if [ "$current_brightness" -eq 0 ]; then
    if [ -f "$SLEEP" ]; then
        read -r grab speaker <"$SLEEP"
        kill "$grab" 2>/dev/null
        [ "$speaker" = speaker ] && /usr/local/bin/audio-output.sh speaker
        rm -f "$SLEEP"
    fi
    echo 0 > "$FREEZE" 2>/dev/null
    if [ -f /tmp/last-brightness ]; then
        cat /tmp/last-brightness > "$BL"
    else
        echo "5" > "$BL"
    fi
    echo "0" > /sys/class/leds/blue:status/brightness
else
    echo "$current_brightness" > /tmp/last-brightness
    echo "0" > "$BL"
    echo "1" > /sys/class/leds/blue:status/brightness
    echo 1 > "$FREEZE" 2>/dev/null
    for e in /sys/class/input/event*; do
        [ "$(cat "$e/device/name")" = "GA36-MB Gamepad" ] && pad=/dev/input/${e##*/}
    done
    evsieve --input "$pad" grab >/dev/null 2>&1 &
    grab=$!
    speaker=$(/usr/local/bin/audio-output.sh get)
    [ "$speaker" = speaker ] && /usr/local/bin/audio-output.sh headphones
    echo "$grab $speaker" > "$SLEEP"
fi
