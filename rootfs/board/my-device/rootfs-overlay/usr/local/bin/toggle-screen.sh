#!/bin/sh
current_brightness=$(cat "/sys/class/backlight/backlight/brightness")

if [ "$current_brightness" -eq 0 ]; then
    if [ -f /tmp/last-brightness ]; then
        cat /tmp/last-brightness > /sys/class/backlight/backlight/brightness
    else
        echo "5" > /sys/class/backlight/backlight/brightness
    fi
    echo "0" > /sys/class/leds/blue:status/brightness
else
    echo "$current_brightness" > /tmp/last-brightness
    echo "0" > /sys/class/backlight/backlight/brightness
    echo "1" > /sys/class/leds/blue:status/brightness
fi
