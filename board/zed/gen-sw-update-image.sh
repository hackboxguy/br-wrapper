#!/bin/sh
UPDATE_IMAGE_NAME=update-image.swu

BOARD_DIR="$( dirname "${0}" )"

cp ${BOARD_DIR}/sw-description ${BINARIES_DIR}

IMG_FILES="sw-description rootfs.ext4.gz"

sed -i "s|version = *.*|version = \"$BRIMAGE_VERSION\";|" ${BINARIES_DIR}/sw-description

#cd into binary director
cd ${BINARIES_DIR}

for f in ${IMG_FILES} ; do
	echo ${f}
done | cpio -ovL -H crc > $UPDATE_IMAGE_NAME

#get back previous directory
cd -
