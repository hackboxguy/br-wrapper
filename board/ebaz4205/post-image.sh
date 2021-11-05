#!/bin/sh
BOARD_DIR="$( dirname "${0}" )"
IMAGE_UENV="uEnv.txt"
IMAGE_FPGA="ebaz4205_top.bin"
cp "${BOARD_DIR}/${IMAGE_UENV}" "${BINARIES_DIR}"
cp "${BOARD_DIR}/${IMAGE_FPGA}" "${BINARIES_DIR}"

#create blank settings image of 64MB in ext3 type to be mounted on target at /mnt/.settings from /dev/mmcblk0p4
#genimage.cfg includes this sttng.ext3 as 4th partition in final sdcard image.
fallocate -l 64M "${BINARIES_DIR}/sttng.ext3"
"${HOST_DIR}/usr/sbin/mkfs.ext3" -F -L STTNG "${BINARIES_DIR}/sttng.ext3"

support/scripts/genimage.sh -c "${BOARD_DIR}/genimage.cfg"
