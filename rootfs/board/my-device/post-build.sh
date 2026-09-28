#!/bin/sh
set -e

TARGET_DIR=$1
INITTAB="${TARGET_DIR}/etc/inittab"

# Add USB serial gadget console if it doesn't already exist
if ! grep -q "^ttyGS0" "$INITTAB"; then
    echo "" >> "$INITTAB"
    echo "# USB Gadget Serial - Standard login" >> "$INITTAB"
    echo "ttyGS0::respawn:/sbin/getty -L ttyGS0 115200 vt100" >> "$INITTAB"
fi

# Disable default getty on tty1
sed -i 's/^tty1::respawn/#tty1::respawn/' "${INITTAB}"

# Modify the triggerhappy init.d script so that it runs as root instead of 'nobody'
sed -i 's/ --user nobody//' "$TARGET_DIR/etc/init.d/S10triggerhappy"
# Move triggerhappy daemon launch way later in the boot process.
# It behaves badly when it's too early.
mv ${TARGET_DIR}/etc/init.d/S10triggerhappy ${TARGET_DIR}/etc/init.d/S99triggerhappy

mkdir ${TARGET_DIR}/etc/player-flags

# SHUTDOWN_LINE='::shutdown:/etc/init.d/S99puppy-bootstrap stop'

# if [ -f "${INITTAB}" ]; then
#     if ! grep -qF 'puppy-bootstrap.pid' "${INITTAB}"; then
#         RCK_LINE='::shutdown:/etc/init.d/rcK'
#         if grep -qF "${RCK_LINE}" "${INITTAB}"; then
#             # Insert this right before the "::shutdown:/etc/init.d/rcK" line
#             ESCAPED_LINE=$(printf '%s\n' "${SHUTDOWN_LINE}" | sed 's/[&/\]/\\&/g')
#             sed -i "\#${RCK_LINE}#i ${ESCAPED_LINE}" "${INITTAB}"
#             echo "post-build.sh: inserted puppy-bootstrap shutdown line into inittab"
#         else
#             echo "post-build.sh: WARNING: rcK shutdown line not found in inittab, appending puppy-bootstrap line at end instead"
#             echo "${SHUTDOWN_LINE}" >> "${INITTAB}"
#         fi
#     fi
# else
#     echo "post-build.sh: WARNING: ${INITTAB} not found, skipping puppy-bootstrap shutdown line injection"
# fi


echo "::respawn:/usr/local/bin/power-manager.sh" >> "$INITTAB"
