#!/bin/sh
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Build the release binaries into dist/: seven Fruit Jam .uf2 files, and the
# Feather ESP32 V2 images.
#
# TWO THINGS THIS DOES DELIBERATELY:
#
# 1. No --fqbn. Each sketch.yaml pins its own optimisation level (invaders
#    Optimize2, the rest Optimize3); passing one here would override every
#    game with a single setting. -Os in particular is not fast enough for
#    this video pipeline and shows up as red screens rather than as a
#    slower picture. See DEVNOTES.md.
#
# 2. --library "$ROOT", and NO config override. This repo IS the Arduino
#    library, so the examples cannot find it the way an installed one would
#    be; --library points the builder at the checkout. Everything else --
#    notably the one dependency, PicoDVI - Adafruit Fork -- comes from your
#    normal sketchbook, so install it the usual way:
#        arduino-cli lib install "PicoDVI - Adafruit Fork"
#
#    This script used to write a throwaway config pinning directories.user
#    to $ROOT, which made sense only while the repo was itself a sketchbook
#    with a libraries/ dir inside it. After the single-library restructure
#    that directory is gone, and the override silently pointed the builder
#    at a sketchbook with no libraries in it -- "PicoDVI.h: No such file or
#    directory" on the first game.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
GAMES="invaders lrescue pacman mspacman btime dkong galaga"

cd "$ROOT"
for g in $GAMES; do
    sk="${g}_fruitjam"
    printf '%-20s ' "$sk"
    if ! arduino-cli compile --library "$ROOT" \
            --output-dir "$HERE" "examples/Games/$sk" \
            > "$HERE/.$g.log" 2>&1; then
        echo "FAILED -- see $HERE/.$g.log"
        exit 1
    fi
    mv "$HERE/$sk.ino.uf2" "$HERE/$sk.uf2"
    # Everything else arduino-cli emits is intermediate. Removing it keeps
    # `gh release upload dist/*.uf2` picking up exactly the right set.
    rm -f "$HERE/$sk.ino."* "$HERE/.$g.log"
    printf 'ok   %s\n' "$(du -h "$HERE/$sk.uf2" | cut -f1 | tr -d ' ')"
done

# --- Feather ESP32 V2 -------------------------------------------------------
#
# A DIFFERENT KIND OF ARTEFACT, deliberately. The ESP32 does not do
# drag-and-drop .uf2; it flashes over serial, and a bare application .bin is
# NOT standalone -- it needs the bootloader at 0x1000 and the partition
# table at 0x8000. The core emits a `.merged.bin` containing all three at
# the right offsets, which is one file flashed at 0x0.
#
# That merged image is padded to the full 8MB of flash, almost all of it
# 0xFF, so it is trimmed to its real content and sector-aligned here: ~504KB
# instead of 8MB, which matters when someone is pushing it through a browser.
ESP_GAMES="pacman mspacman"
for g in $ESP_GAMES; do
    sk="${g}_featheresp32"
    printf '%-20s ' "$sk"
    if ! arduino-cli compile --library "$ROOT" \
            --fqbn esp32:esp32:adafruit_feather_esp32_v2 \
            --output-dir "$HERE" "examples/Games/$sk" \
            > "$HERE/.$g-esp32.log" 2>&1; then
        echo "FAILED -- see $HERE/.$g-esp32.log"
        exit 1
    fi
    python3 - "$HERE" "$sk" <<'TRIM'
import sys, pathlib
here, sk = pathlib.Path(sys.argv[1]), sys.argv[2]
d = (here / f"{sk}.ino.merged.bin").read_bytes()
n = (len(d.rstrip(b"\xff")) + 0xFFF) & ~0xFFF
assert d[0x1000] == 0xE9 and d[0x8000:0x8002] == b"\xaa\x50" and d[0x10000] == 0xE9, \
    "merged image is not laid out as expected -- refusing to ship it"
(here / f"{sk}.bin").write_bytes(d[:n])
TRIM
    # NOTE the glob: the core emits both `NAME.ino.bin` and
    # `NAME.ino_flashed.bin` -- dot AND underscore. Matching only
    # "$sk.ino." left the underscore one behind, and
    # `gh release upload dist/*.bin` would have shipped it.
    rm -f "$HERE/$sk.ino"* "$HERE/.$g-esp32.log"
    printf 'ok   %s\n' "$(du -h "$HERE/$sk.bin" | cut -f1 | tr -d ' ')"
done

echo
echo "built into $HERE:"
ls -1 "$HERE"/*.uf2 "$HERE"/*.bin
