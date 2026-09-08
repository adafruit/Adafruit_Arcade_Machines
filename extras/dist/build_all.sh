#!/bin/sh
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Build all seven game sketches into dist/ as release-ready .uf2 files.
#
# TWO THINGS THIS DOES DELIBERATELY:
#
# 1. No --fqbn. Each sketch.yaml pins its own optimisation level (invaders
#    Optimize2, the rest Optimize3); passing one here would override every
#    game with a single setting. -Os in particular is not fast enough for
#    this video pipeline and shows up as red screens rather than as a
#    slower picture. See DEVNOTES.md.
#
# 2. --library "$ROOT". This repo IS the Arduino library, so the examples
#    cannot find it the way an installed library would be found; --library
#    points the builder at the checkout. A throwaway arduino-cli config
#    still sets directories.user, which is only how the third-party
#    dependency (PicoDVI - Adafruit Fork) gets discovered when it has been
#    installed into this repo's own gitignored libraries/ dir. If yours
#    lives in your normal sketchbook instead, that works too -- set
#    ARDUINO_DIRECTORIES_USER and this script will not override it.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
GAMES="invaders lrescue pacman mspacman btime dkong galaga"

CFG="$HERE/.arduino-cli.yaml"
{
    echo "board_manager:"
    echo "    additional_urls:"
    echo "        - https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json"
    echo "directories:"
    echo "    user: ${ARDUINO_DIRECTORIES_USER:-$ROOT}"
} > "$CFG"

cd "$ROOT"
for g in $GAMES; do
    sk="${g}_fruitjam"
    printf '%-20s ' "$sk"
    if ! arduino-cli --config-file "$CFG" compile --library "$ROOT" \
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
rm -f "$CFG"

echo
echo "built into $HERE:"
ls -1 "$HERE"/*.uf2
