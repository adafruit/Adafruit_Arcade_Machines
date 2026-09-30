<!--
SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
SPDX-License-Identifier: MIT
-->

# gameboy_fruitjam

The original Game Boy (DMG) on the Adafruit Fruit Jam, **with the microSD
card as the cartridge**. See the [top-level README](../../../README.md) for
the framework and the general build steps; this page covers what is
specific to the console. The design, decisions and remaining phases are in
[`extras/CONSOLES_PLAN.md`](../../../extras/CONSOLES_PLAN.md).

The emulator core is [Peanut-GB](https://github.com/deltabeard/Peanut-GB)
(MIT) with [minigb_apu](https://github.com/baines/MiniGBS) for sound (MIT),
vendored unchanged apart from one documented patch
([`src/machines/gb/core/VENDORED.md`](../../../src/machines/gb/core/VENDORED.md)).

## The cartridge

Put **one** Game Boy ROM, `.gb`, in a folder called `/cart` on an SD card
(FAT32, **MBR** partition scheme -- not GPT/exFAT). The first `.gb` found
is the game that boots.

- **Cartridge types:** ROM-only, MBC1, MBC2, MBC3 and MBC5, up to 4 MB.
  The ROM is loaded into the Fruit Jam's PSRAM at start-up.
- **Battery saves** work the way the cartridge's did: when the game saves,
  the save RAM is written to `/cart/<rom name>.sav`, in the same raw format
  PC emulators use, so saves move between the two. There is no save button
  and there are no save states. A save takes about half a second, written
  a little each frame so the picture never stutters.
- **Settings are remembered per game:** the rotation, palette and 3x/1x
  you last chose are saved to `/cart/<rom name>.fruitjam.cfg`, 3 seconds
  after your last change, and used again at the next power-up. It's a
  short text file you can read or edit on a computer; delete it to go back
  to the defaults. The Feather keeps its own, `.feather.cfg`, so one card
  works in both.
- **Game Boy Color** games are not supported yet.
- **Tested:** Tetris, Kirby's Dream Land, and The Legend of Zelda: Link's
  Awakening (including saves). The compatibility list is in
  CONSOLES_PLAN.md.

## Controls

| Game Boy | Fruit Jam GPIO | USB pad (Nintendo layout) | Retro-bit Genesis pad |
|---|---|---|---|
| D-pad | A3 / A4 / D8 / D9 | D-pad | D-pad |
| A | D10 | A | B |
| B | A2 | B | A |
| Start | D6 | Start | Start |
| Select | A5 | Select | Mode |

- **USB gamepads** plug into either Type-A port. They need **Tools → USB
  Stack → Adafruit TinyUSB**, which this sketch's `sketch.yaml` selects
  (the IDE may not read it). With the default stack the console runs on
  the GPIO buttons alone. Tested: a Mantapad (SNES-style), a Retro-bit
  Genesis 8-button pad and a DualShock 4 (Circle is A, Cross is B).
- **Button 1 (STRETCH)** switches the picture between 3x and 1x (see
  below).
- **Button 2 (ROTATE)** cycles the picture's rotation. The console boots
  in landscape.
- **Button 3 (MIRROR)** cycles the colour palette:
  1. DMG green (the default);
  2. greys;
  3. Game Boy Pocket;
  4. the colours a Game Boy Color would give the game, chosen from its
     title.

## Picture and sound

The 160×144 screen boots at 3x: each Game Boy pixel an exact 3×3 block,
480×432 upright or 432×480 rotated (the full height), centred on the
640×480 output. **Button 1** switches to 1x, 320×288 with each pixel a 2×2
block, and back. Either works in all four rotations. 3x needs the default
HSTX video; on the PicoDVI fallback (`-DARCADE_FRUITJAM_PICODVI`) the
picture is 1x and the button does nothing. Sound is the full four-channel Game Boy APU.

## Boot error screens

| Colour | Meaning |
|---|---|
| Red | No SD card, or it would not mount |
| Yellow | Card mounted, but no `.gb` in `/cart` |
| Magenta | The ROM failed its header check, is an unsupported cartridge type, or is too big |

The serial port (115200) repeats the reason every two seconds while the
error screen is up.
