<!--
SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
SPDX-License-Identifier: MIT
-->

# Vendored NES core: nofrendo, from retro-go

Everything else in this directory is **nofrendo**, the NES emulator by
Matthew Conte and others, as maintained in
[retro-go](https://github.com/ducalex/retro-go)
(`retro-core/components/nofrendo`), copied at commit
`4ced120669750ca7228fd0414211430c1d923166` (2026-09) with **three
patches**, below. The core was chosen by measurement: see
`extras/CONSOLES_PLAN.md`, "NES core candidates", and DEVNOTES #135.

Left out: `docs/`, which bundles a zip of third-party test ROMs, and
retro-go's `CMakeLists.txt`.

## Licence

- **nofrendo's own files** carry Matthew Conte's header: *version 2 of the
  GNU Library General Public License*.
- **retro-go's additions** (mappers, the palette and game database, a few
  headers) carry no header of their own. retro-go distributes this
  component under the GNU GPL version 2 (`COPYING`, and retro-go's README:
  "Everything in this project is licensed under the GPLv2").
- **LGPL-2.0, section 3,** lets a copy be taken under the ordinary GPL
  version 2 instead. So the directory is declared **GPL-2.0-only** in
  `REUSE.toml`, with every author from `CREDITS`.

This, like every core in the plan, is a finding for Adafruit's legal
review, not a legal conclusion (`extras/CONSOLES_PLAN.md`, "Core
licensing"). It links only into the NES sketch: the library is archived
(`dot_a_linkage`), so an arcade or Game Boy build pulls in none of these
objects.

## The three patches

Each is a unified diff against the upstream files, applied in this order
(`patch -p1 -d <nofrendo>`). The NES spike harness applies the same three
(`extras/tools/nes_test/fetch.sh`), so the harness and the library compile
identical code.

| Patch | What | Found by |
|---|---|---|
| `nofrendo-zp-wrap.patch` | `ZP_READWORD` read a 16-bit word straight out of zero-page memory, so a `(zp,X)` or `(zp),Y` pointer at `$FF` took its high byte from `$100` (the stack) instead of `$00`. Every `(zp,X)`/`(zp),Y` opcode got it wrong. | blargg `instr_test-v5` `08-ind_x`, `09-ind_y` |
| `nofrendo-mmc1-surom.patch` | MMC1 took PRG A18 from CHR bit 4 whenever the cart had `>= 16` 16 KB banks, i.e. every 256 KB cart, not only 512 KB SUROM boards; a CHR write could move the program past the end of the ROM. Now `> 16`. | reading the mapper while chasing `official_only.nes` (256 KB) |
| `nofrendo-includes.patch` | Mechanical. retro-go builds with the component root on the include path, so `nes/` includes `"config.h"` and `"mappers/mappers.h"`, and the mappers include `"nes/nes.h"`. An Arduino library has only `src/` on the path, so these become relative (`"../config.h"`, `"../nes/nes.h"`, ...). No code changes. | the library build |

To re-vendor: fetch the commit above, delete `docs/` and
`CMakeLists.txt`, apply the three patches in order, and compare. The
result must equal this directory, apart from these `.patch` files and this
note.

## How the library drives it

Only `../nes_core.c` includes nofrendo's headers. It does not call
`nes_emulate()`, which runs a whole frame (~7 ms on the RP2350) without
returning: the display queue holds ~2.2 ms of picture. It steps the same
per-scanline loop itself, through nofrendo's public functions, so the
machine can feed the display between lines (DEVNOTES #135).
