#!/bin/sh
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Build the NES machine host harness. See main.cpp and ../README.md.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../../../src"
OBJ="$HERE/build"
OUT="$HERE/nes_host"
INC="-I$SRC -I$HERE/../host_common/shim -I$HERE/../host_common -I$SRC/hal"

mkdir -p "$OBJ"
# The vendored core and its wrapper are C. -w for the vendored core only:
# its warnings are not ours to fix (core/VENDORED.md).
NC="$SRC/machines/nes/core"
(cd "$OBJ" && cc -O2 -g -std=gnu11 -w $INC -c "$NC"/nofrendo.c "$NC"/nes/*.c "$NC"/mappers/*.c)
cc -O2 -g -std=gnu11 -Wall $INC -c "$SRC/machines/nes/nes_core.c" -o "$OBJ/nes_core.o"

c++ -O2 -g -std=c++17 -Wall -Wno-unused-parameter $INC \
    "$SRC/machines/nes"/*.cpp \
    "$SRC/console"/*.cpp \
    "$SRC/cart"/*.cpp \
    "$SRC/hal"/*.cpp \
    "$HERE/../host_common/hal_host.cpp" \
    "$HERE/../host_common/host_ppm.cpp" \
    "$HERE/main.cpp" \
    "$OBJ"/*.o \
    -o "$OUT"
echo "built: $OUT"
