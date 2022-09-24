#!/bin/sh

set -u
set -e

#copy default aws-iot-pubsub-agent.conf to /boot partition
cp ${BR2_EXTERNAL_BRWRAPPER_PATH}/board/aws-iot-demo/fs-overlay/etc/aws-iot-pubsub-agent.conf ${BINARIES_DIR}/

# Add a console on tty1
if [ -e ${TARGET_DIR}/etc/inittab ]; then
    grep -qE '^tty1::' ${TARGET_DIR}/etc/inittab || \
	sed -i '/GENERIC_SERIAL/a\
tty1::respawn:/sbin/getty -L  tty1 0 vt100 # HDMI console' ${TARGET_DIR}/etc/inittab
fi
