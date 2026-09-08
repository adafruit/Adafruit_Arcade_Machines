#!/bin/sh
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Build the Pac-Man host harness. See ../README.md.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../../src"
OBJ="$HERE/build"
OUT="$HERE/pacman_host"

INC="-I$SRC \
     -I$HERE/../host_common/shim \
     -I$HERE/../host_common \
     -I$SRC/hal \
     -I$SRC/cpu/z80 \
     -I$SRC/machines/pacman"

mkdir -p "$OBJ"

# z80.c is C (z80.h carries its own extern "C" guards), so build it as C
# and link -- same split the Arduino build uses.
cc -O2 -g -std=c11 -Wall $INC -c "$SRC/cpu/z80/z80.c" -o "$OBJ/z80.o"

c++ -O2 -g -std=c++17 -Wall -Wno-unused-parameter $INC \
    "$SRC/machines/pacman"/*.cpp \
    "$SRC/hal"/*.cpp \
    "$HERE/../host_common/hal_host.cpp" \
    "$HERE/../host_common/host_ppm.cpp" \
    "$HERE/main.cpp" \
    "$OBJ/z80.o" \
    -o "$OUT"

echo "built: $OUT"
