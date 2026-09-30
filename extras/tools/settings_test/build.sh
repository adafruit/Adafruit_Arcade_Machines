#!/bin/sh
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Build the settings-file conformance runner. See ../README.md.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../../../src"
OUT="$HERE/settings_test"

c++ -O1 -g -std=c++17 -Wall -Wextra \
    -I"$SRC" \
    "$SRC/settings/settings.cpp" \
    "$SRC/storage/extent_lock.cpp" \
    "$HERE/fake_storage.cpp" \
    "$HERE/main.cpp" \
    -o "$OUT"

echo "built: $OUT"
