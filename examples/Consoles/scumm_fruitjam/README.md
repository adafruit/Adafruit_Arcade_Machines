<!--
SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
SPDX-License-Identifier: MIT
-->

# scumm_fruitjam

LucasArts **SCUMM** adventures on the Adafruit Fruit Jam: Loom, Monkey
Island 1 (EGA), Indiana Jones and the Last Crusade (EGA) and the free
demos. See the [top-level README](../../../README.md) for the framework
and the general build steps; this page covers what is specific to SCUMM.

The engine is **fruitjam-scumm**, Mikey Sklar's cut-down copy of
[ScummVM](https://www.scummvm.org/) 2.2.0's SCUMM engine (v3/v4 games) from
[mikeysklar/fruitjam-arcade](https://github.com/mikeysklar/fruitjam-arcade),
vendored with three documented patches
([`src/machines/scumm/core/VENDORED.md`](../../../src/machines/scumm/core/VENDORED.md)).
**This sketch's binary contains GPL-3.0-or-later code** (ScummVM's own
files are GPL-2.0-or-later). It links only into this sketch.

No game data comes with this library. Use your own copy of a game, or
ScummVM's free demos (fruitjam-arcade's `ports/scumm/get_testdata.sh`
fetches them).

## The card

A SCUMM game is a **folder**, plus a small **marker** file in `/cart` that
names it, as upstream's launcher does it. On an SD card (FAT32, **MBR**
partition scheme -- not GPT/exFAT):

```
/scumm/loom/        00.LFL ... 99.LFL     the game's files, as they came
/cart/loom.scumm    /scumm/loom           a text file holding the folder's path
```

- The **alphabetically first** `.scumm` marker in `/cart` is the game that
  boots. An empty marker means the folder `/cart/<marker name>/`.
- A marker written for upstream's CircuitPython launcher, which mounts the
  card at `/sd`, works as it is: `/sd/scumm/loom` is read as `/scumm/loom`.
- File names are matched without regard to case.
- **Saves** are the game's own files (`loom.s01`, ...), written into the
  game folder.
- **Settings are remembered per game** in `/cart/<marker name>.fruitjam.cfg`,
  a short text file you can edit on a computer:
  - `music = pcspk | adlib | none` -- the PC speaker (the default), or
    AdLib (FM) music for DOS Loom and Indy 3, which sounds much richer;
    read once, at start-up.
  - `subtitles = on | off`
  - `pointer = 0 | 1 | 2` -- how fast the pointer moves.
- **Tested on hardware:** Loom (DOS EGA, floppy) with AdLib music, for an
  hour. Upstream also runs the Monkey Island, Indy 3 and Loom demos,
  Passport to Adventure, and Loom for the PC Engine CD.

## Controls

A **USB mouse and keyboard** work as they would on a PC, and so do the pad
and the Fruit Jam's buttons, all at once. On the pad, the D-pad moves the
pointer.

| Action | Fruit Jam GPIO | USB pad (Nintendo layout) |
|---|---|---|
| Move the pointer (it speeds up while held) | A3 / A4 / D8 / D9 | D-pad |
| Left click | D10 | A |
| Right click | A2 | B |
| Escape (skip a cutscene) | A1 | Y |
| Space (pause), on release | D6 | Start |
| F5, the game's save/load screen, on release | A5, or Button 1 | Select |
| "." (skip the current line of dialogue) | D7 | X |
| Quick save | Button 2 | -- |
| Quick load | Button 3 | -- |

- **Saving:** F5 opens the game's own save/load screen. Saving there wants
  a typed name, so it needs a keyboard; without one, use **quick save**
  (Button 2), which saves to slot 1, listed as "Fruit Jam". Loading from
  the game's screen works with the pad or the mouse.
- **USB mouse:** moves the pointer (two mouse counts per game pixel, as
  the picture is drawn at 2x) and clicks, left and right.
- **USB keyboard** (US layout): letters, digits, punctuation, F1-F12,
  Enter, Esc, Backspace, Tab, Space, Delete and the arrows reach the game,
  so you can type save names, and in Loom play a draft by typing its
  notes. It must
  offer the standard boot keyboard interface, as most do (tested: a
  Keychron K8). A keyboard with an off switch is invisible until it's on.
- **USB devices** plug into either Type-A port, with **Tools → USB Stack →
  Adafruit TinyUSB** (which `sketch.yaml` selects); with two ports, a
  keyboard and a mouse fill them, or use a wireless combo receiver. Tested
  with a Mantapad, a Keychron K8 and a USB mouse. On a Retro-bit Genesis
  pad, Escape and "." are on the GPIO buttons only.

## Picture and sound

The 320x200 picture is shown at 2x, 640x400, centred in the 640x480 output.
On the default HSTX video, core 1 draws it straight from a framebuffer, so
a room loading from the card (up to ~270 ms) never stalls the picture. On
the PicoDVI fallback (`-DARCADE_FRUITJAM_PICODVI`) it goes through the
canvas, and the picture pauses while a room loads.

The engine makes its audio a frame at a time, and after a slow frame (a
full redraw, a room load) the sketch tops the sound back up with extra
audio from the engine's mixer, so pans don't stutter the music. The cost is
about a tenth of a second of extra sound delay. Sound plays from the
headphone jack/speaker and over the display cable.

## Boot error screens

| Colour | Meaning |
|---|---|
| Red | No SD card, or it would not mount |
| Yellow | No `.scumm` marker in `/cart`, or the folder it names has no files |
| Magenta | No SCUMM game recognised in the folder, no PSRAM for the engine's 2 MB, or the engine failed to start |

The serial port (115200) repeats the reason every two seconds while the
error screen is up.

## Crash reports

This sketch turns on the Fruit Jam's crash reporting
(`src/boards/fruitjam/fruitjam_crash.h`). If the board faults, or core 0
stops answering for 2 seconds, it saves where, reboots, and repeats the
report on serial (`LAST RESET WAS A FAULT / STALL / HANG ...`). Map the
address to a function with
`arm-none-eabi-addr2line -f -C -e <build>/scumm_fruitjam.ino.elf 0x<pc>`.
Building with `-DSCUMM_PROFILE` adds a sampling profiler that prints where
the engine spends its time every 10 seconds (`extras/DEVNOTES.md` #165).

## Uploads

Uploads work as for any sketch. Before v2.18.0's USB host fix
(`extras/DEVNOTES.md` #166) the board often ignored the upload's 1200-baud
reset while this sketch ran; if that ever comes back, hold BOOT while
plugging in the USB and copy the `.uf2` to the drive that appears, and
build with `-DTEST_RESET_PROBE` to see what the serial line receives
(DEVNOTES #170).
