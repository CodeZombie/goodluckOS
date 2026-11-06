#!/bin/sh
# usb-mode.sh get|mtp|network
# What the USB OTG port offers a PC, next to the serial console:
#   mtp      HOME over MTP (default)
#   network  USB network (NCM): the console is 192.168.36.1, SSH as root
# The mode is kept across reboots. Setting a mode restarts the USB gadget, also when it is
# the current one, so a failed switch can be retried.

MODE_FILE=/etc/player-flags/usb-mode

cur=$(cat "$MODE_FILE" 2>/dev/null)
[ "$cur" = network ] || cur=mtp

case "$1" in
    get)         echo "$cur"; exit 0 ;;
    mtp|network) new=$1 ;;
    *)           echo "usage: $0 get|mtp|network" >&2; exit 2 ;;
esac

echo "$new" > "$MODE_FILE"
/etc/init.d/S30usb-gadget restart
