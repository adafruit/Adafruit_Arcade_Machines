<!--
SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
SPDX-License-Identifier: MIT
-->

# Vendored Game Boy core

Everything else in this directory is third-party code, copied from upstream
with **one rename and one patch**, both below. It is MIT-licensed (see the
copyright and license lines in each file, and `REUSE.toml` at the repository
root).

| File | Upstream | Commit | SHA-256 of the file here |
|------|----------|--------|--------------------------|
| `peanut_gb.h` | [deltabeard/Peanut-GB](https://github.com/deltabeard/Peanut-GB) | `d0bcca771c83a2638c93a9ae61f3d51f226dc905` (2026-09-21) | upstream `cf7777068492474d084f7dca0b00724d4dcdeab7f9a6c91a5d3a82e60d1cf728`; **as patched here** `3a918e456517d6b5ae1b00d67ee8061cd514eace3159725a7e4a82fea43308c1` |
| `minigb_apu.h` | [deltabeard/minigb_apu](https://github.com/deltabeard/minigb_apu) | `344d9814c0d834faf1a26ec97b57bedddefed8aa` (2025-08-15) | `a03aab1a934af8248524b0e7b6dd7bb531859bad84164cb564298a8604ae9b6a` |
| `minigb_apu.c.inc` | same, upstream name `minigb_apu.c` | same | `0bdab624ded8c5655010215cfc8fd3e328f19b51865c33fc979f35fedaabebf1` |

**Vendored from upstream on purpose, not from PicoPlus's `pico-peanutGB`.**
That repository is GPL-3.0 as a whole and carries locally modified copies
(its `peanut_gb.h` is 4,372 lines against upstream's 4,045), so taking the
files from there would cloud the license and hide unknown changes.

## The one change: `minigb_apu.c` is renamed `minigb_apu.c.inc`

The Arduino builder compiles every `.c` and `.cpp` file under `src/` for
every sketch that uses this library, arcade games included, and a sketch
cannot pass `-D` flags into a library's compile. `minigb_apu.h` refuses to
compile (`#error`) unless an audio sample format macro is defined first. As
a plain `.c` file it would break all fourteen arcade builds.

As `.c.inc` the builder ignores it. The one translation unit that needs it
defines the format and sample rate and then `#include`s it. The contents
are byte-for-byte upstream's; the hash above lets anyone check.

`peanut_gb.h` is header-only, so it is compiled only where included.
Exactly one translation unit may include it without `PEANUT_GB_HEADER_ONLY`
(here, `../gameboy_core.c`).

## The one patch: "halt yield" in `peanut_gb.h`

`peanut_gb-halt-yield.patch` is the whole change, against the upstream file
above; every changed spot in `peanut_gb.h` is marked `ADAFRUIT PATCH (halt
yield)`.

**The problem.** While the CPU is halted with no interrupt pending, upstream
`__gb_step_cpu()` keeps looping inside one call until an interrupt arrives.
With the LCD switched off, no VBLANK interrupt ever comes, so one call ran
for up to 2.2 ms on the Fruit Jam during Kirby's Dream Land's level load.
Nothing is drawn while the core holds the CPU, so the 32-line DVI queue ran
dry and red lines appeared (DEVNOTES #128).

**The change.** A halted CPU returns at a frame boundary and after at most
`PEANUT_GB_HALT_YIELD_CYCLES` (default 4560, ten LCD lines) of halted time.
The next call sees a halt still pending with no interrupt, and re-enters the
timing loop with the step size it was about to use (saved in a new
`struct gb_s` field, `halt_next_cycles`), so a yield only splits upstream's
loop across two calls. It must not be treated as a wake-up: upstream's
interrupt code assumes a halt only ever returns once an interrupt has
occurred.

**How it was checked**, against the unpatched upstream file:

- blargg's `cpu_instrs` pass and `dmg-acid2` matches its reference.
- `gb_host` on Tetris and `dmg-acid2`: seven frames and 50 s of audio,
  bit-identical. (Tetris never takes the resume path, so this checks only
  that running code is untouched.)
- All 115 mooneye test ROMs (MIT), with a counter proving the resume path
  runs (15-66 resumes in the four halt tests): 114 bit-identical in result
  and final frame, pass count unchanged at 36. The exception is
  `madness/mgb_oam_dma_halt_sprites`, a deliberately extreme stress test
  that fails on both: identical through frame 14, then different, for a
  reason not established.
- On the Fruit Jam, Kirby's load screen: longest single core call down
  from 2249 us to 214 us, and no starvation (with the two changes in
  `../gameboy_core.c` and `../gameboy_machine.cpp` that DEVNOTES #128 also
  describes).

**Worth sending upstream.** The unbounded halt loop affects any Peanut-GB
host that has to do other work within a frame.

## Updating

Fetch the same three files from a newer upstream commit, keep the rename,
reapply the patch (`patch -p1 < peanut_gb-halt-yield.patch` from this
directory), update the commit and hash columns above, and re-run
`extras/tools/gb_test` (blargg's `cpu_instrs` and `dmg-acid2`) before
anything else. If upstream has fixed the halt loop itself, drop the patch.
