// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// The build mode for the vendored SCUMM engine in this library. Upstream's
// host harness (ports/scumm/Makefile.host) builds every engine file with
// -DFJ_HOST -DFJ_ADLIB -DNDEBUG and `-include src/backend/fj_host_arena.h`;
// an Arduino library can't pass flags, so extras/tools/scumm_vendor/
// revendor.py makes this header the first include of every vendored .c
// and .cpp instead. See ../VENDORED.md.
//
// FJ_HOST, not upstream's board mode (FJ_NATMOD): that one is a
// CircuitPython native module with no C library, so it defines malloc,
// free, setjmp and printf itself. Linked into this library those would
// replace the C library's in EVERY sketch. FJ_HOST keeps the C library
// and renames the engine's allocator calls to fj_host_malloc() and
// friends, which allocate from the arena handed to fj_core_init(). The
// coroutine still uses its ARM assembler path, since fj_coro.c picks that
// by architecture, not by mode.
//
// The engine's `new` goes to the arena only if the sketch replaces the
// global operator new (as upstream's libc/fj_cxx.cpp does). That can't
// live in the library: with dot_a_linkage the linker would pull it into
// every sketch to satisfy `new`. machines/scumm/scumm_new.h has it, for
// the SCUMM sketch alone to include.
#ifndef FJ_ARDUINO_H
#define FJ_ARDUINO_H

#ifndef FJ_HOST
#define FJ_HOST 1
#endif
#ifndef FJ_ADLIB
#define FJ_ADLIB 1
#endif
// NDEBUG as upstream, except on the ESP32: ESP-IDF's assert() still
// parses its argument with NDEBUG set (to keep variables "used"), and
// the engine hides some of those variables behind #ifndef NDEBUG
// (common/hashmap.h's old_size), so there the asserts stay on.
#if defined(ESP_PLATFORM)
#undef NDEBUG
#elif !defined(NDEBUG)
#define NDEBUG 1
#endif

// FAST RAM for the two things the engine touches most: its coroutine stack
// and its 8-bit screen, in on-chip RAM rather than the arena (PSRAM). On
// the RP2350, PSRAM and flash share one 16 KB XIP cache.
//   - The stack: every interrupt taken while the engine runs (USB host,
//     I2S, timers) works on it too.
//   - The screen: render() converts every dirty row of it to RGB565 each
//     frame, a whole 64 KB pass on a pan, which in PSRAM also evicted the
//     code that ran next. A profile of steady play put render() at ~32% of
//     the engine's frame (DEVNOTES #165).
// ../scumm-fast-ram.patch adds the hooks to fj_core.cpp and osystem.cpp;
// machines/scumm/scumm_machine.cpp provides them. The 16-bit screen of
// Loom PC Engine (128 KB) doesn't fit and stays in the arena.
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *fj_arduino_stack_alloc(size_t n);
void *fj_arduino_screen_alloc(size_t n);   // zeroed
void fj_arduino_screen_free(void *p);
#ifdef __cplusplus
}
#endif
#define FJ_STACK_ALLOC(n) fj_arduino_stack_alloc(n)
#define FJ_SCREEN_ALLOC(n) fj_arduino_screen_alloc(n)
#define FJ_SCREEN_FREE(p) fj_arduino_screen_free(p)

#include "fj_host_arena.h"

#endif
