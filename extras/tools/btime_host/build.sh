#!/bin/sh
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Build the Burger Time host harness. See ../README.md.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../../../src"
OBJ="$HERE/build"
OUT="$HERE/btime_host"

INC="-I$SRC \
     -I$HERE/../host_common/shim \
     -I$HERE/../host_common \
     -I$SRC/hal \
     -I$SRC/cpu/m6502 \
     -I$SRC/machines/btime"

mkdir -p "$OBJ"

# m6502.c is C (m6502.h carries its own extern "C" guards), so build it as C
# and link -- same split the Arduino build uses. Both of this machine's CPUs
# are instances of this one core.
cc -O2 -g -std=c11 -Wall $INC -c "$SRC/cpu/m6502/m6502.c" -o "$OBJ/m6502.o"

c++ -O2 -g -std=c++17 -Wall -Wno-unused-parameter $INC \
    "$SRC/machines/btime"/*.cpp \
    "$SRC/hal"/*.cpp \
    "$HERE/../host_common/hal_host.cpp" \
    "$HERE/../host_common/host_ppm.cpp" \
    "$HERE/main.cpp" \
    "$OBJ/m6502.o" \
    -o "$OUT"

echo "built: $OUT"
