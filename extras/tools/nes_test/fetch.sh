#!/bin/sh
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Fetch what the NES spike harness needs, at pinned commits, into
# gitignored directories. Nothing here is vendored yet: this is the spike
# that decides which core gets vendored (extras/CONSOLES_PLAN.md, "NES core
# candidates").
#
#   cores/nofrendo/  nofrendo as maintained in retro-go (LGPL-2 / GPL-2)
#   cores/fixnes/    fixNES (MIT)
#   roms/            blargg's NES test ROMs, via christopherpow/nes-test-roms
#                    (no licence statement; freely distributed, not committed)
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
RETROGO_COMMIT=4ced120669750ca7228fd0414211430c1d923166
FIXNES_COMMIT=156fcaca9f4cd9c23d423737994c45cfb05d16ca
TESTROMS_COMMIT=95d8f621ae55cee0d09b91519a8989ae0e64753b

mkdir -p "$HERE/cores" "$HERE/roms"

if [ ! -d "$HERE/cores/nofrendo" ]; then
    tmp=$(mktemp -d)
    git -C "$tmp" init -q
    git -C "$tmp" remote add origin https://github.com/ducalex/retro-go.git
    git -C "$tmp" fetch -q --depth 1 origin "$RETROGO_COMMIT"
    git -C "$tmp" checkout -q FETCH_HEAD -- retro-core/components/nofrendo
    mv "$tmp/retro-core/components/nofrendo" "$HERE/cores/nofrendo"
    rm -rf "$tmp"
    # Our fixes, each found by a test ROM (see the patch headers).
    for p in "$HERE"/patches/nofrendo-*.patch; do
        patch -s -p1 -d "$HERE/cores/nofrendo" < "$p"
    done
fi

if [ ! -d "$HERE/cores/fixnes" ]; then
    git -C "$HERE/cores" init -q fixnes
    git -C "$HERE/cores/fixnes" remote add origin https://github.com/FIX94/fixNES.git
    git -C "$HERE/cores/fixnes" fetch -q --depth 1 origin "$FIXNES_COMMIT"
    git -C "$HERE/cores/fixnes" checkout -q FETCH_HEAD
fi

B=https://raw.githubusercontent.com/christopherpow/nes-test-roms/$TESTROMS_COMMIT
for f in instr_test-v5/official_only.nes instr_timing/instr_timing.nes \
         cpu_interrupts_v2/cpu_interrupts.nes ppu_vbl_nmi/ppu_vbl_nmi.nes \
         apu_test/apu_test.nes mmc3_test_2/rom_singles/1-clocking.nes \
         mmc3_test_2/rom_singles/4-scanline_timing.nes; do
    out="$HERE/roms/$(basename "$f")"
    [ -f "$out" ] || curl -sSfL -o "$out" "$B/$f"
done
ls -l "$HERE/roms"
