#!/bin/sh -e

BOARD_DIR="$(dirname $0)"

cp -r "${BOARD_DIR}"/boot-overlay/* "${BINARIES_DIR}/rpi-firmware"

rm -rf "${TARGET_DIR}/var/log/*"
mkdir -p "${TARGET_DIR}/var/log"

rm -rf "${TARGET_DIR}/var/run"
rm -rf "${TARGET_DIR}/run"
mkdir -p "${TARGET_DIR}/run"
ln -s ../run "${TARGET_DIR}/var/run"
