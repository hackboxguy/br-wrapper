#!/bin/sh
set -u
set -e

# Add a console on tty1
if [ -e ${TARGET_DIR}/etc/inittab ]; then
    grep -qE '^tty1::' ${TARGET_DIR}/etc/inittab || \
        sed -i '/GENERIC_SERIAL/a\
tty1::respawn:/sbin/getty -L  tty1 0 vt100 # HDMI console' ${TARGET_DIR}/etc/inittab
# systemd doesn't use /etc/inittab, enable getty.tty1.service instead
elif [ -d ${TARGET_DIR}/etc/systemd ]; then
    mkdir -p "${TARGET_DIR}/etc/systemd/system/getty.target.wants"
    ln -sf /lib/systemd/system/getty@.service \
       "${TARGET_DIR}/etc/systemd/system/getty.target.wants/getty@tty1.service"
fi

# First, ensure pip is installed
echo "Installing pip..."
${HOST_DIR}/bin/python3 -m ensurepip --upgrade

# Now install Python packages
echo "Installing Python packages..."
${HOST_DIR}/bin/python3 -m pip install \
    --no-cache-dir \
    --disable-pip-version-check \
    --target="${TARGET_DIR}/usr/lib/python3.11/site-packages" \
    pandas==2.1.3 \
    shapely==2.0.3 \
    colour-science==0.4.2

echo "Python packages installation complete"
