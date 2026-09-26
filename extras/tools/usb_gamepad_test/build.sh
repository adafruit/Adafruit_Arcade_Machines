#!/bin/sh
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Build the USB gamepad decoder test. See main.cpp and ../README.md.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../../../src"

c++ -O2 -g -std=c++17 -Wall -Wextra -I"$SRC" \
    "$SRC/input/usb_gamepad_decode.cpp" "$HERE/main.cpp" -o "$HERE/usb_gamepad_test"
echo "built: $HERE/usb_gamepad_test"
