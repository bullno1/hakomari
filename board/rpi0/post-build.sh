#!/bin/sh -e

BOARD_DIR="$(dirname $0)"

cp -r "${BOARD_DIR}"/boot-overlay/* "${BINARIES_DIR}/rpi-firmware"
mkdir -p "${TARGET_DIR}/dev/shm"
mkdir -p "${TARGET_DIR}/var/log"
