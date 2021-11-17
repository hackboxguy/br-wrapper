#!/bin/sh
BOARD_DIR="$( dirname "${0}" )"
IMAGE_FPGA="ebaz4205_top.bin"
#copy fpga configuration file to /boot directory of ext4 rootfs, bootloader would pick either from rootfs-1 or rootfs2
#based on the boot marker
cp "${BOARD_DIR}/${IMAGE_FPGA}" "${TARGET_DIR}/boot/"

#this script gets called by buildroot after building everything(but before creating rootf.tar)
#here we are copying version information to /etc/sw-versions file on the target rootfs.
#contents of /etc/sw-versions are used by swupdate utility during sw update action.
[ -z "$BRIMAGE_VERSION" ] && BRIMAGE_VERSION="0.0.1" #if version is not provided from buildroot make, then just use default
echo "software $BRIMAGE_VERSION" > ${TARGET_DIR}/etc/sw-versions
echo "rootfs-1 $BRIMAGE_VERSION" >> ${TARGET_DIR}/etc/sw-versions
echo "rootfs-2 $BRIMAGE_VERSION" >> ${TARGET_DIR}/etc/sw-versions
