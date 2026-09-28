<!--
SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
SPDX-License-Identifier: MIT
-->

# HDMI audio on the Fruit Jam

Sound through the HDMI cable to the TV's speakers, using
[Adafruit DVI Audio](https://github.com/mikeysklar/Adafruit_DVI_Audio), a
thin MIT wrapper around [fliperama86/pico_hdmi](https://github.com/fliperama86/pico_hdmi)
(The Unlicense). Started 2026-09-27.

## What the library is

- **A complete video stack on the RP2350's HSTX peripheral**, not an add-on
  to PicoDVI. It outputs 640x480 at 60 Hz and puts HDMI audio packets ("data
  islands") in the blanking intervals.
- **Video is pulled:** core 1's per-line interrupt calls a scanline callback
  for each 640-pixel line.
- **Audio is pushed:** 4-frame stereo packets, encoded by the caller and
  queued. The rate is 32, 44.1 or 48 kHz. The video interrupt drains the
  queue at an exact rate.
- **Core 1 has a background-task slot**, which pico_hdmi documents as
  "typically used for audio processing".
- **Status:** v0.0.1, first committed 2026-09-26, not yet in Library
  Manager.

## How it differs from the Fruit Jam today

| | Today | pico_hdmi |
|---|---|---|
| Video hardware | PicoDVI's PIO path: PIO 0, three state machines, TMDS encoded in software on core 1, which is parked in `hal_video_run()` for good | HSTX, with a hardware TMDS encoder; core 1 only fills lines |
| Video model | push: core 0 renders 320-pixel lines into a 32-line queue | pull: a per-line callback on core 1 |
| Audio | I2S to the TLV320 DAC, 22,050 Hz mono, pulled by its interrupt through each machine's fill callback | encoded packets pushed to a queue |
| Clock | 252 MHz via `fruitjam_set_sys_clock_khz()`, which also retimes the PSRAM | 252 MHz at 1.15 V via the wrapper's `begin()`, without the PSRAM retime |

## How it would fit

- **Video:** our 32-line queue feeds pico_hdmi's scanline callback. It pops
  one 320-pixel line per two output lines and doubles it. The machines, the
  HAL contract and queue-paced frames stay as they are.
- **Audio:** a pump in core 1's background task calls the machine's
  existing fill callback at 22,050 Hz, repeats each sample (44.1 kHz is
  exactly twice that, so there's no real resampling), encodes and queues.
  None of it runs on core 0.
- **Side benefits:** PIO 0 is freed, and core 1 gains most of the time it
  now spends encoding TMDS.
- **The clock stays ours:** pico_hdmi's C API is used directly, not the
  wrapper's `begin()`, so the PSRAM retime is kept.

## Decisions (2026-09-27)

1. **Sound goes to both** the TV over HDMI and the headphone jack/DAC, the
   same samples to each.
2. **Develop now, depend on publication.** Build against a local copy of the
   library; add it to `depends=` once it is in Library Manager (Limor's
   rule: dependencies come from Library Manager, not vendored). No release
   ships the HDMI path before then.
3. **Spike first, then decide** on replacing the video backend, with
   numbers.

## Steps

1. **Spike** (`examples/SelfTest/hdmi_audio_test_fruitjam`): a test pattern
   through our kind of line queue, and a 44.1 kHz tone made by doubling a
   22,050 Hz stream. Check that the TV plays it; measure missed lines, the
   callback's cost on core 1, and the encode cost per audio packet.
   **Done 2026-09-27, DEVNOTES #147:** the TV plays the tone cleanly, and
   the picture is right. 60 fps, 0 missed lines, audio delivered at
   44.1 kHz with the queue never below 199 of 200. The line callback takes
   31% of core 1 and the audio encoding 10%, about 41% in all, against
   all of core 1 for PicoDVI's software TMDS encoding today.
2. **An HSTX video backend** for the Fruit Jam behind the existing HAL,
   with PicoDVI kept as a build-time fallback. A/B all nine Fruit Jam
   sketches for frame time and starvation. **Done 2026-09-27, DEVNOTES
   #148:** `src/boards/fruitjam/hal_video_fruitjam_hstx.cpp`, selected by
   `-DARCADE_FRUITJAM_HSTX`. All nine match PicoDVI on the same frames
   (Donkey Kong +10% work at first; +0.7% once core 1's loop sleeps, #149), and all nine look right on
   the TV. There's a desync watchdog for pico_hdmi's intermittent stream
   loss.
3. **An HDMI audio backend**, with the machines' fill callbacks unchanged
   and the pump on core 1, feeding the DAC too. **Working 2026-09-27,
   DEVNOTES #149:** an audio tap in the I2S driver feeds a ring that core 1
   pumps to HDMI at 44.1 kHz. Pac-Man and Burger Time are clean and in sync
   from the TV and the jack together. Burger Time, the heaviest game, runs
   14.97 ms against PicoDVI's 15.06. Still to check: the other seven with
   HDMI audio, and a mid-game battery save.
4. **Docs, `depends=`, release**, once the library is in Library Manager.
   Expose pico_hdmi's DVI-only mode for displays that don't sync with data
   islands.

## Risks

- The library is days old.
- TV compatibility can only be judged on real sets.
- The video swap touches every Fruit Jam sketch; hence the fallback, and
  the A/B before it becomes the default.
- The wrapper's build options (`dvi_audio_config.h`) are fixed inside it.
  pico_hdmi's hardware pixel doubling ("native pixel mode") needs one of
  them changed, so step 2 uses the callback doubling instead.
