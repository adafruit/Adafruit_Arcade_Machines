<!--
SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
SPDX-License-Identifier: MIT
-->

# Audio over HDMI on the Fruit Jam — feasibility notes

**Status: investigated, NOT implemented. No code has been written.** This is a
record of what was found on 2026-09-16 so that the question does not have to be
re-researched from scratch, and so that anyone picking it up starts from the
measurements rather than from the assumptions.

Prompted by [pico-infonesPlus](https://github.com/fhoedemakers/pico-infonesPlus),
which gained HDMI audio output and lists the Fruit Jam among its supported
boards.

---

## The one-line version

Feasible, and the Fruit Jam hardware is already right for it, but it is a
**video-backend replacement, not an audio feature**. Nothing in the current
stack can emit HDMI audio, and no amount of work in `hal_audio_*` can change
that.

## Why this is a video problem

DVI has no audio channel at all. HDMI carries audio in TERC4-encoded *data
islands* injected during the blanking intervals, interleaved with the video
TMDS stream. Only the thing generating that stream can generate them — so the
audio path is decided entirely by which video driver is in use.

Three facts, each checked against the installed libraries rather than assumed:

- `src/boards/fruitjam/hal_video_fruitjam.cpp` uses the **PIO-bitbanged**
  libdvi vendored in `PicoDVI - Adafruit Fork` — `dvi_serialiser.pio`,
  `tmds_encode.S`, software TMDS encoding running on Core 1. There is not one
  occurrence of HSTX anywhere in that library. **We are running the RP2040-era
  software encoder on an RP2350B.**
- That same libdvi has no audio code whatsoever (no data-island, TERC4, or
  audio-packet anything).
- `Adafruit_DVI_HSTX` — a separate library, already installed here — *is*
  HSTX-based, and also has no audio.

Both available paths are audio-incapable, by different routes.

## What pico-infonesPlus actually uses

Not PicoDVI. Its HDMI audio comes from
[`fliperama86/pico_hdmi`](https://github.com/fliperama86/pico_hdmi), an
HSTX-native RP2350 driver doing hardware TMDS plus scheduled data-island
injection. Its README is explicit that this is HSTX-only, and that DVI mode has
no audio while HDMI mode does. Licensed Unlicense (public domain).

---

## Path A — move to HSTX, add `pico_hdmi` (recommended if this is ever done)

**The pins are already correct.** `common_dvi_pin_configs.h`'s
`adafruit_fruitjam_cfg` puts the HDMI connector on GPIO 12 (clock) and 14/16/18
(data), which is exactly the RP2350 HSTX block. Same physical pins, different
peripheral driving them. **No hardware change, no board revision, no jumper.**

**The system clock is NOT the obstacle it first appears to be.** `pico_hdmi`'s
docs quote "126 MHz system clock" for 640x480, which reads like halving the
252 MHz every game here is budgeted against. That is a property of how that
library chooses to clock itself, not of HSTX:
`Adafruit_dvhstx.cpp`'s `display_setup_clock_preinit()` decouples the two,
running `clk_sys` from **PLL_USB at 240 MHz** while repurposing the *sys* PLL to
drive `clk_hstx` at `bit_clk_khz >> 1` (126 MHz for 640x480). `pico_hdmi`'s own
scanline-callback budget is quoted as "~800-1600 cycles at 126-252 MHz", so
252 MHz is supported outright. **Assume the current clock survives.**

**The real prize is not the audio.** Hardware TMDS would free Core 1 completely
— `dvi_scanbuf_main_16bpp()` is currently Core 1's entire job — and free PIO0
(DVI today; I2S is PIO1 SM0). For Galaga, whose measured worst case is 15156us
against a 16500us budget (#107), that is worth more than a second audio output
is. **If this is ever evaluated, evaluate it on that basis, with audio as the
bonus.**

**The architecture probably survives.** `pico_hdmi` is scanline-callback based,
like libdvi, so `hal_video`'s acquire/submit contract has a plausible mapping.
Going via `Adafruit_DVI_HSTX` *instead* would be a framebuffer rewrite:
320x240x16bpp is 150 KB, and Galaga compiles to 222,956 of 524,288 bytes of
globals (measured, `opt=Optimize2`), so a single buffer fits with ~148 KB to
spare and a double buffer does not.

**The sample rate lines up by luck.** All seven machines run at 22050 Hz mono
and HDMI supports 44.1 kHz — exactly 2x. Sample doubling, no resampler.

**What it would cost:**

- Vendoring a Pico SDK CMake library into an Arduino library, guarded so the
  ESP32 build is untouched (the established `#if defined(BOARD)` pattern).
- SPDX headers on the vendored files, against the project's 198/198 REUSE
  record. Unlicense is compatible; the bookkeeping still has to be done.
- DMA channel claiming, against the I2S pair and the SD card.
- A full rewrite of `hal_video_fruitjam.cpp`, then re-validation of 7 games x
  4 rotations on hardware. This is the bulk of the work and it is not small.

**The unmeasured risk:** the h-blank callback window is ~6us. The per-scanline
rotation/aspect math in the machine renderers has never been measured against a
budget that tight. **Measure that before committing to this path**, not after.

## Path B — keep PIO libdvi, add data islands to it

[`ikjordan/PicoDVI`](https://github.com/ikjordan/PicoDVI) forks the same libdvi
with audio added, credited to mlorenzati and
[shuichitakano](https://github.com/shuichitakano/pico_lib). Smaller blast
radius: same scanline queue, same everything else.

**Probably a trap.** Its own README concedes audio "works with some limitations
of cpu usage", and CPU on the PIO path is precisely what is short here — Core 1
is already fully spent on software TMDS at 252 MHz. Looks cheaper than Path A,
is very likely riskier.

---

## Before doing any of this, be clear about the payoff

The Fruit Jam already has working speaker **and** headphone output through the
TLV320DAC3100, configured in `hal_audio_fruitjam.cpp` and verified on hardware
for all seven games. **HDMI audio is a one-cable convenience for TVs, not a
missing capability.** Nothing is broken today.

Two questions that cannot be settled without hardware:

1. Whether the monitors people actually use accept our timing as HDMI (with
   AVI/audio InfoFrames and ACR) rather than falling back to DVI.
2. Whether the rotation math fits the h-blank budget (above).
