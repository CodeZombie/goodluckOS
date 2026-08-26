#!/bin/sh
set -e

TARGET_DIR=$1
INITTAB="${TARGET_DIR}/etc/inittab"
INTERFACES_FILE="${TARGET_DIR}/etc/network/interfaces"

# Add USB serial gadget console if it doesn't already exist
if ! grep -q "^ttyGS0" "$INITTAB"; then
    echo "" >> "$INITTAB"
    echo "# USB Gadget Serial - Standard login" >> "$INITTAB"
    echo "ttyGS0::respawn:/sbin/getty -L ttyGS0 115200 vt100" >> "$INITTAB"
fi

# Disable default getty on tty1
sed -i 's/^tty1::respawn/#tty1::respawn/' "${INITTAB}"

echo "" >> "$INITTAB"
echo "# Puppy Bootstrap" >> "$INITTAB"
echo "tty1::respawn:/bin/su - player -c '/usr/local/bin/puppy-bootstrap.sh'" >> "$INITTAB"


echo "auto usb0" >> "$INTERFACES_FILE"
echo "iface usb0 inet static" >> "$INTERFACES_FILE"
echo "    address 192.168.7.2" >> "$INTERFACES_FILE"
echo "    netmask 255.255.255.0" >> "$INTERFACES_FILE"

# Modify the triggerhappy init.d script so that it runs as root instead of 'nobody'
sed -i 's/ --user nobody//' "$TARGET_DIR/etc/init.d/S10triggerhappy"
