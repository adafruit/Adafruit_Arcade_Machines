<!--
SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
SPDX-License-Identifier: MIT
-->

# Vendored SCUMM engine: fruitjam-scumm

Everything else in this directory is **fruitjam-scumm**, Mikey Sklar's
cut-down copy of [ScummVM](https://www.scummvm.org/) 2.2.0's SCUMM engine
(LucasArts' v3/v4 adventures: Loom, Monkey Island 1 EGA, Indy 3 EGA and
the free demos) with a small C interface, `backend/fj_core.h`. It comes from
[mikeysklar/fruitjam-arcade](https://github.com/mikeysklar/fruitjam-arcade)
(`ports/scumm/src`), copied at commit
`b7cb16491ede66e5dde6e56471fea80647221ec9` (2026-10-05). Upstream's own
`NOTES.md` and `STATUS.md` explain how it works and what it runs; this
project's bring-up is `extras/DEVNOTES.md` #165-#168.

## Licence

- **The folder** is distributed under **GPL-3.0-or-later** (`LICENSE`),
  as upstream ships `ports/scumm`.
- **ScummVM's files** keep their headers, "GPL version 2 or later", which
  allows that. Their authors are in `AUTHORS` and `COPYRIGHT`, copied from
  upstream.
- **Upstream's backend** (`backend/fj_*`, `osystem.cpp`) is Mikey's, under
  GPL-3.0-or-later.

`REUSE.toml` declares the directory that way. As with the NES core, this
is a finding for Adafruit's legal review, not a legal conclusion. It
links only into the SCUMM sketch: the library is archived
(`dot_a_linkage`), so no other sketch pulls in any of these objects (a
Pac-Man build is byte-for-byte the same size with or without them).

## What is different from upstream

Made by `extras/tools/scumm_vendor/revendor.py`, and nothing else:

1. **Left out:** `modscumm.c` (the CircuitPython binding) and `libc/` (the
   C library a native module lacks; Arduino has newlib).
2. **Two renames,** because archived objects need unique file names
   (DEVNOTES #126): `scumm/file.cpp` → `scumm/scumm_file.cpp` and
   `scumm/util.cpp` → `scumm/scumm_util.cpp`. Their headers keep their
   names.
3. **Includes made relative.** Upstream builds with `-Isrc`, so files
   include `"common/array.h"`, `"scumm/costume.h"` and so on. An Arduino
   library has only its own `src/` on the path, so every include that
   names a file in this tree becomes relative to the including file
   (`"../common/array.h"`).
4. **One line added** at the top of every `.c` and `.cpp`:
   `#include ".../backend/fj_arduino.h"`. It stands in for the host
   build's `-DFJ_HOST -DFJ_ADLIB -DNDEBUG -include fj_host_arena.h`. That
   header is ours (MIT) and says why the host mode is the right one here:
   the board mode, `FJ_NATMOD`, defines `malloc` and friends itself, which
   in this library would replace the C library's in every sketch.
5. **Three patches,** applied last by the script in name order. Each
   change in them is marked `Adafruit Arcade Machines:`.
   - `scumm-fast-ram.patch`: hooks (`FJ_STACK_ALLOC`, `FJ_SCREEN_ALLOC`,
     `FJ_SCREEN_FREE`) for the engine's coroutine stack and its 8-bit
     screen, which upstream takes from the arena (PSRAM on the board).
     `fj_arduino.h` points them at static buffers in on-chip RAM
     (`../scumm_machine.cpp`); on the RP2350 PSRAM shares the flash's small
     cache, and these two are touched by every interrupt and every frame.
     The defaults keep upstream's behaviour.
   - `scumm-mix-extra.patch`: `fj_core_mix_extra()`, more audio without
     advancing the game, so the glue can catch the output up after a slow
     frame (DEVNOTES #165).
   - `scumm-save-screen.patch`: the game's own save/load screen. Plain F5
     now prepares the v3 games' save snapshot (ScummVM's Alt-F5, because
     desktop ScummVM keeps F5 for its own menu; without it every typed
     save failed), and `listSavefiles()` takes its pattern from the C
     string (the pattern's NUL kept its `*`, so the load list was always
     empty). DEVNOTES #169.

To re-vendor: check out fruitjam-arcade at the new commit, run
`extras/tools/scumm_vendor/revendor.py <checkout>`, and update the commit
above. It leaves `fj_arduino.h` and this note alone.

## What the library adds around it

`../` holds the glue: the arena from PSRAM, file access over the board's
storage HAL, the framebuffer and its scan-out, audio, and the pad as a
mouse. The engine's C++ `new` reaches the arena only through
`../scumm_new.h`, which the SCUMM sketch includes; it can't live in the
library for the same `dot_a_linkage` reason as `malloc` above.
