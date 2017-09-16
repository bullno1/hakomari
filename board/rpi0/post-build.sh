#!/bin/sh -e

BOARD_DIR="$(dirname $0)"

cp -r "${BOARD_DIR}"/boot-overlay/* "${BINARIES_DIR}/rpi-firmware"
