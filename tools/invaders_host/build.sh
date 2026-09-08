#!/bin/sh
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Build the Space Invaders host harness. See ../README.md.
#
# MACHINE_SRC / OUT can be overridden to build a SECOND binary from a
# different copy of ArcadeMachine_Invaders -- that is how an A/B digest
# comparison against a previous revision is done (see main.cpp's header):
#
#   MACHINE_SRC=/tmp/old/src/machines/invaders OUT=/tmp/invaders_host_old ./build.sh
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../../src"
OBJ="$HERE/build"
MACHINE_SRC="${MACHINE_SRC:-$SRC/machines/invaders}"
OUT="${OUT:-$HERE/invaders_host}"

INC="-I$SRC \
     -I$HERE/../host_common/shim \
     -I$HERE/../host_common \
     -I$SRC/hal \
     -I$SRC/cpu/i8080 \
     -I$MACHINE_SRC"

mkdir -p "$OBJ"

# i8080.c is C (i8080.h carries its own extern "C" guards), so build it as C
# and link -- same split the Arduino build uses.
cc -O2 -g -std=c11 -Wall $INC -c "$SRC/cpu/i8080/i8080.c" -o "$OBJ/i8080.o"

c++ -O2 -g -std=c++17 -Wall -Wno-unused-parameter $INC \
    "$MACHINE_SRC"/*.cpp \
    "$SRC/hal"/*.cpp \
    "$HERE/../host_common/hal_host.cpp" \
    "$HERE/../host_common/host_ppm.cpp" \
    "$HERE/main.cpp" \
    "$OBJ/i8080.o" \
    -o "$OUT"

echo "built: $OUT"
