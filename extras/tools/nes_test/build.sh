#!/bin/sh
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Build the NES spike harness, one binary per core. Run ./fetch.sh first.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
NF="$HERE/cores/nofrendo"

cc -O2 -g -std=gnu11 -w -I"$HERE" -I"$NF" -I"$NF/nes" \
    "$HERE/harness.c" "$HERE/core_nofrendo.c" \
    "$NF/nofrendo.c" "$NF"/nes/*.c "$NF"/mappers/*.c \
    -o "$HERE/nes_nofrendo"
echo "built: $HERE/nes_nofrendo"

# fixNES, as its libretro build compiles it: every source but the OpenAL
# ones (audio.c, alhelpers.c), whose audioUpdate() core_fixnes.c replaces.
FX="$HERE/cores/fixnes"
OBJ="$HERE/build/fixnes"
mkdir -p "$OBJ"
(cd "$OBJ" && cc -O2 -g -std=gnu11 -w -D__LIBRETRO__ -I"$HERE/shim" -I"$FX" -c \
    $(ls "$FX"/*.c | grep -v -e '/audio.c$' -e '/alhelpers.c$') "$FX"/mapper/*.c "$FX"/unzip/*.c)
cc -O2 -g -std=gnu11 -Wall -I"$HERE" -c "$HERE/harness.c" -o "$OBJ/harness.o"
cc -O2 -g -std=gnu11 -Wall -I"$HERE" -c "$HERE/core_fixnes.c" -o "$OBJ/core_fixnes.o"
cc "$OBJ"/*.o -lz -o "$HERE/nes_fixnes"
echo "built: $HERE/nes_fixnes"
