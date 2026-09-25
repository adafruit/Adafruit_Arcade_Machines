#!/bin/sh
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Build the Game Boy core conformance runner. See main.c and ../README.md.
# -Wall stays on, but the vendored core's own warnings are not ours to fix
# (VENDORED.md: upstream files are kept byte-identical), so they are shown,
# not turned into errors.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
CORE="$HERE/../../../src/machines/gb/core"

cc -O2 -g -std=c11 -Wall -I"$CORE" "$HERE/main.c" -o "$HERE/gb_test"
echo "built: $HERE/gb_test"
