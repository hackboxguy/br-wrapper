#!/bin/sh
BOARD_DIR="$( dirname "${0}" )"

IMAGE_UENV="uEnv.txt" #TODO: 
IMAGE_FPGA="zed_top.bin" #TODO: for future
#cp "${BOARD_DIR}/${IMAGE_UENV}" "${BINARIES_DIR}"
#cp "${BOARD_DIR}/${IMAGE_FPGA}" "${BINARIES_DIR}"

#create blank settings image of 64MB in ext3 type to be mounted on target at /mnt/.settings from /dev/mmcblk0p4
#genimage.cfg includes this sttng.ext3 as 4th partition in final sdcard image.
fallocate -l 64M "${BINARIES_DIR}/sttng.ext3"
"${HOST_DIR}/usr/sbin/mkfs.ext3" -F -L STTNG "${BINARIES_DIR}/sttng.ext3"

FIRST_DT=$(sed -n \
	           's/^BR2_LINUX_KERNEL_INTREE_DTS_NAME="\([a-z0-9\-]*\).*"$/\1/p' \
		              ${BR2_CONFIG})
[ -z "${FIRST_DT}" ] || ln -fs ${FIRST_DT}.dtb ${BINARIES_DIR}/devicetree.dtb

support/scripts/genimage.sh -c "${BOARD_DIR}/genimage.cfg"
