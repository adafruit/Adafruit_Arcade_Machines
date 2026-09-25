#!/bin/sh
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Build the Game Boy machine host harness. See main.cpp and ../README.md.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../../../src"
OBJ="$HERE/build"
OUT="$HERE/gb_host"

INC="-I$SRC \
     -I$HERE/../host_common/shim \
     -I$HERE/../host_common \
     -I$SRC/hal"

mkdir -p "$OBJ"

# gameboy_core.c is C (it compiles the vendored core), so build it as C and
# link -- the same split the Arduino build uses.
cc -O2 -g -std=c11 -Wall $INC -c "$SRC/machines/gb/gameboy_core.c" -o "$OBJ/gameboy_core.o"

c++ -O2 -g -std=c++17 -Wall -Wno-unused-parameter $INC \
    "$SRC/machines/gb"/*.cpp \
    "$SRC/cart"/*.cpp \
    "$SRC/hal"/*.cpp \
    "$HERE/../host_common/hal_host.cpp" \
    "$HERE/../host_common/host_ppm.cpp" \
    "$HERE/main.cpp" \
    "$OBJ/gameboy_core.o" \
    -o "$OUT"

echo "built: $OUT"
