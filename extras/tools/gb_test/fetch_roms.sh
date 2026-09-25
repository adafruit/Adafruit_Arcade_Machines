#!/bin/sh
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Fetch the third-party test ROMs into roms/ (gitignored). They are not
# committed: dmg-acid2 is MIT but is still someone else's binary, and blargg's
# test ROMs carry no license statement at all.
#
#   cpu_instrs.gb      blargg's CPU instruction tests (via retrio/gb-test-roms)
#   dmg-acid2.gb       Matt Currie's PPU test, and its reference image
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$HERE/roms"
cd "$HERE/roms"

curl -sSfL -o cpu_instrs.gb \
    https://raw.githubusercontent.com/retrio/gb-test-roms/master/cpu_instrs/cpu_instrs.gb
curl -sSfL -o dmg-acid2.gb \
    https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb
curl -sSfL -o reference-dmg.png \
    https://raw.githubusercontent.com/mattcurrie/dmg-acid2/master/img/reference-dmg.png

ls -l "$HERE/roms"
