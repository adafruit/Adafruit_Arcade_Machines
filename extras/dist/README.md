<!--
SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
SPDX-License-Identifier: MIT
-->

# `dist/` — built firmware, for testing and for release assets

**Not checked in.** The binaries are covered by `.gitignore`; only this
README and `build_all.sh` are tracked. The binaries here are build output, and on GitHub they
belong as **release assets**, not as repository contents.

## Flashing a Fruit Jam game

Hold **BOOT** while connecting USB (or hold BOOT and tap **RESET**), then
copy the `.uf2` onto the `RP2350` drive that appears.

Each binary still needs its game's microSD card — no ROM data is baked in.
See the per-game README for the `/rom/` and `/samples/` layout it expects.

## Flashing the Feather ESP32 V2

`pacman_featheresp32.bin` is a **different kind of artefact** and does not
drag-and-drop. The ESP32 flashes over serial, so use Adafruit's browser
tool — no software to install:

<https://learn.adafruit.com/circuitpython-with-esp32-quick-start/web-serial-esptool>

Follow that guide, but **flash `pacman_featheresp32.bin` at offset `0x0`**
(the guide's own example uses a CircuitPython image; the procedure is the
same). Chrome or Edge only — Safari and Firefox have no Web Serial.

**Why offset 0 and why one file.** A bare ESP32 application binary is not
standalone: it needs a bootloader at `0x1000` and a partition table at
`0x8000`. This image is the core's `merged.bin`, which contains all three
at their correct offsets, trimmed from the 8MB it is padded to down to the
~504KB that is actually content. `build_all.sh` asserts the three magic
bytes are where they belong before writing it, rather than trusting the
trim.

This board also needs the Pac-Man microSD card, exactly as the Fruit Jam
build does.

## Rebuilding the whole set

```sh
./dist/build_all.sh
```

Each sketch pins its own optimisation level in its `sketch.yaml`
(`default_fqbn`), and the script deliberately does NOT pass `--fqbn`, so each
game builds the way it was measured. **This matters**: Space Invaders is
pinned to `Optimize2` and the rest to `Optimize3`, and `-Os` is not fast
enough for this pipeline — it produces red screens rather than a clean
slower picture. See DEVNOTES.md.

The builds are plain defaults: no `TEST_ROTATION`, `TEST_STRETCH` or
`TEST_AUTOSTART`. Those exist for measurement and would ship a game stuck in
one rotation or playing itself.

## Uploading to a release

```sh
gh release create vX.Y.Z dist/*.uf2 --title "..." --notes "..."
# or, for an existing release:
gh release upload vX.Y.Z dist/*.uf2
```
