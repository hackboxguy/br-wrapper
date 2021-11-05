#!/bin/sh
BOARD_DIR="$( dirname "${0}" )"
IMAGE_UENV="uEnv.txt"
IMAGE_FPGA="ebaz4205_top.bin"
cp "${BOARD_DIR}/${IMAGE_UENV}" "${BINARIES_DIR}"
cp "${BOARD_DIR}/${IMAGE_FPGA}" "${BINARIES_DIR}"
support/scripts/genimage.sh -c "${BOARD_DIR}/genimage.cfg"
