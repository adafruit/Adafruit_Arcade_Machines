<!--
SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
SPDX-License-Identifier: MIT
-->

# Vendored Game Boy core

Everything else in this directory is third-party code, copied from upstream
**unmodified** except for one rename. It is MIT-licensed (see the copyright
and license lines in each file, and `REUSE.toml` at the repository root).

| File | Upstream | Commit | SHA-256 of the file here |
|------|----------|--------|--------------------------|
| `peanut_gb.h` | [deltabeard/Peanut-GB](https://github.com/deltabeard/Peanut-GB) | `d0bcca771c83a2638c93a9ae61f3d51f226dc905` (2026-09-21) | `cf7777068492474d084f7dca0b00724d4dcdeab7f9a6c91a5d3a82e60d1cf728` |
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

`peanut_gb.h` needs no change: it is header-only, so it is compiled only
where included. Exactly one translation unit may include it without
`PEANUT_GB_HEADER_ONLY`.

## Updating

Fetch the same three files from a newer upstream commit, keep the rename,
update the commit and hash columns above, and re-run
`extras/tools/gb_test` (blargg's `cpu_instrs` and `dmg-acid2`) before
anything else.
