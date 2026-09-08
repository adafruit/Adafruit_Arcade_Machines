#!/bin/sh
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Build the screen-geometry conformance runner. See ../README.md.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../../../src"
OUT="$HERE/geom_test"

c++ -O1 -g -std=c++17 -Wall \
    -I"$SRC" \
    -I"$SRC/hal" \
    "$SRC/hal/arcade_video_geom.cpp" \
    "$HERE/main.cpp" \
    -o "$OUT"

echo "built: $OUT"
