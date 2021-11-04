#!/bin/sh
BOARD_DIR="$( dirname "${0}" )"
IMAGE_UENV="uEnv.txt"
cp "${BOARD_DIR}/${IMAGE_UENV}" "${BINARIES_DIR}"
support/scripts/genimage.sh -c "${BOARD_DIR}/genimage.cfg"
