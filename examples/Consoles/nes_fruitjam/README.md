<!--
SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
SPDX-License-Identifier: MIT
-->

# nes_fruitjam

The NES on the Adafruit Fruit Jam, **with the microSD card as the
cartridge**. See the [top-level README](../../../README.md) for the
framework and the general build steps; this page covers what is specific
to the NES. The design and its history are in
[`extras/CONSOLES_PLAN.md`](../../../extras/CONSOLES_PLAN.md) and
DEVNOTES #135-#138.

The emulator core is **nofrendo**, as maintained in
[retro-go](https://github.com/ducalex/retro-go), vendored with three small
patches ([`src/machines/nes/core/VENDORED.md`](../../../src/machines/nes/core/VENDORED.md)).
It is licensed **GPL-2.0-only**, unlike the rest of this library (MIT):
**a firmware built from this sketch includes GPL code.** The library is
archived when a sketch links, so the arcade games and the Game Boy never
link nofrendo.

## The cartridge

Put **one** NES ROM, `.nes` (iNES format), in a folder called `/cart` on
an SD card (FAT32, **MBR** partition scheme -- not GPT/exFAT). The first
`.nes` found is the game that boots.

- **Mappers:** the 59 nofrendo has, including NROM, MMC1, UxROM, CNROM,
  MMC3 and AxROM. ROMs up to 2 MB are loaded into PSRAM; those of 128 KB
  or less are copied into SRAM, which is slightly faster.
- **Battery saves** work the way the cartridge's did. When the game writes
  its battery RAM and then leaves it alone for a second, the RAM is saved
  to `/cart/<rom name>.sav`, in the same raw format PC emulators use. A
  save takes about half a second, written a little each frame so the
  picture never stutters. There is no save button, and there are no save
  states.
- **Tested on hardware:** Super Mario Bros., Super Mario Bros. 3 (MMC3,
  from PSRAM), and The Legend of Zelda (MMC1, with its save surviving a
  power cycle). Kirby's Adventure, Metroid and Final Fantasy run in the
  host harness (`extras/tools/nes_host`).

## Controls

| NES | Fruit Jam GPIO | USB pad (Nintendo layout) | Retro-bit Genesis pad |
|---|---|---|---|
| D-pad | A3 / A4 / D8 / D9 | D-pad | D-pad |
| A | D10 | A | B |
| B | A2 | B | A |
| Start | D6 | Start | Start |
| Select | A5 | Select | Mode |

- **USB gamepads** plug into either Type-A port. They need **Tools → USB
  Stack → Adafruit TinyUSB**, which this sketch's `sketch.yaml` selects
  (the IDE may not read it). With the default stack the NES runs on the
  GPIO buttons alone.
- **Button 1 (STRETCH)** toggles aspect correction for the NES's 8:7
  pixels. It starts off (1:1).
- **Button 2 (ROTATE)** cycles the picture's rotation. The console boots
  in landscape.
- **Button 3 (MIRROR)** cycles nofrendo's six colour palettes.

## Picture and sound

The 256x240 picture is shown 1:1 (512x480 on the 640x480 output), centred.
With aspect correction on, it is 292 pixels wide. Rotated 90 degrees, the
8 outermost columns at each side are cropped to fit (the part a TV's
overscan hid). Sound is nofrendo's APU: two pulse channels, triangle,
noise and DMC.

## Boot error screens

| Colour | Meaning |
|---|---|
| Red | No SD card, or it would not mount |
| Yellow | Card mounted, but no `.nes` in `/cart` |
| Magenta | Not an iNES image, an unsupported mapper, or too big |

The serial port (115200) repeats the reason every two seconds while the
error screen is up.
