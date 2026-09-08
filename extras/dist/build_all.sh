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

echo
echo "built into $HERE:"
ls -1 "$HERE"/*.uf2
