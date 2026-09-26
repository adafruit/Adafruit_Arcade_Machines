#!/bin/sh
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Build the Fruit Jam NES spike sketch (fruitjam_spike/): copy the fetched,
# patched nofrendo into its src/ (gitignored), then compile it against this
# library. Run ./fetch.sh first. Upload with:
#   arduino-cli upload -p PORT --input-dir build/fruitjam_spike fruitjam_spike
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
LIB=$(cd "$HERE/../../.." && pwd)
SK="$HERE/fruitjam_spike"

rm -rf "$SK/src"
mkdir -p "$SK/src"
cp -R "$HERE/cores/nofrendo" "$SK/src/nofrendo"
rm -rf "$SK/src/nofrendo/docs" "$SK/src/nofrendo/CMakeLists.txt"
cp "$HERE/nes_frame_crc.h" "$SK/src/"

# nofrendo includes "nes/nes.h" from its mappers, so its root goes on the
# include path -- the COPY arduino-cli compiles, under the build path's
# sketch/, not this folder: with both reachable, #pragma once sees two
# different files and every nofrendo header is defined twice.
BP="$HERE/build/fruitjam_spike"
NFI="$BP/sketch/src/nofrendo"
arduino-cli compile --library "$LIB" --build-path "$BP" \
    --build-property "compiler.c.extra_flags=-I$NFI -w" \
    --build-property "compiler.cpp.extra_flags=-I$NFI" \
    "$SK" "$@"
