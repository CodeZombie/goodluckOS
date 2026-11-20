#!/bin/sh
# osd-notify.sh <title> <text> - a panel over any app for 1.5 s, drawn by the Gallium HUD patch in Mesa
# (System Settings -> Show system feedback). Its font is ASCII only.
printf '%s\n%s\n' "$1" "$2" > /dev/shm/hud-osd.tmp && mv /dev/shm/hud-osd.tmp /dev/shm/hud-osd
