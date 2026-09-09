// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// The one place that knows which chip this is being built for.
//
// Everything under src/cpu/ and src/machines/ is meant to be portable C --
// a CPU interpreter and a game's memory map have no business naming a
// vendor SDK. Only src/boards/ is allowed to be hardware-specific, because
// that is the whole point of the board axis. This header exists so the two
// genuinely platform-dependent things those layers DO need can be asked for
// abstractly.
//
// Adding a platform means adding an arm here and nothing else.
#ifndef ARCADE_PORTABILITY_H
#define ARCADE_PORTABILITY_H

#include <stdint.h>

// -----------------------------------------------------------------------
// ARCADE_FAST_FUNC(name) -- place a function where it executes without a
// flash stall. Used in the NAME position:
//
//     static void ARCADE_FAST_FUNC(fill_audio_buffer)(int32_t *b, int n)
//
// Why the name position and not a plain attribute: it must produce a
// SEPARATE linker section per function. This library compiles every machine
// into every example and relies on --gc-sections to discard the six the
// build does not use; parking them all in one shared section would defeat
// that and pull all seven games' audio into every binary. Verified: RAM is
// byte-identical across the whole example set precisely because that
// discarding works.
//
// This matters for correctness, not just size. An XIP cache-miss stall
// inside the audio ISR is long enough to starve the video scanline queue,
// which shows up on screen as coloured glitch lines -- see extras/
// DEVNOTES.md and invaders_pico's "Red horizontal lines when sounds play".
//
// ARCADE_ISR_TIME_US() -- a 32-bit microsecond count readable FROM AN ISR.
// The contract is strict: it must be an inline register or counter read,
// never a function call that might live in flash. On RP2 the obvious
// choices (time_us_64/time_us_32) are NOT inline -- checked with `nm` on a
// linked .elf, both are separately-compiled pico-sdk functions in flash, and
// calling one from the audio ISR reintroduces exactly the stall above. An
// early version of this project's ISR instrumentation did that before it was
// caught. Wraps every ~71 minutes; unsigned subtraction handles it.
//
// ARCADE_FAST_SECTION(tag) -- the same placement as ARCADE_FAST_FUNC, but
// in ATTRIBUTE position, for code that declares functions separately from
// defining them:
//
//     static void exec_opcode(z80 *z, uint8_t op) ARCADE_FAST_SECTION("z80");
//
// `tag` names one linker section shared by that module's hot functions. The
// CPU cores use this rather than the per-function form because a core is
// all-or-nothing: if a build uses the Z80 at all it wants every one of
// these, and grouping them keeps the section count down.
//
// ARCADE_TIME_US64() -- monotonic 64-bit microseconds. Normal code paths
// only; no ISR guarantee, and it may be a real function call. 64-bit
// because at least one caller derives an emulated cycle count from elapsed
// time and must not wrap during a long session.
// -----------------------------------------------------------------------

#if defined(ARDUINO_ARCH_RP2040) || defined(PICO_ON_DEVICE)

  #include "pico.h"
  #include "pico/time.h"
  #include "hardware/timer.h"

  #define ARCADE_FAST_FUNC(name)     __not_in_flash_func(name)
  #define ARCADE_FAST_SECTION(tag)   __attribute__((section(".time_critical." tag)))
  #define ARCADE_ISR_TIME_US()    (timer_hw->timerawl)
  #define ARCADE_TIME_US64()      time_us_64()

#elif defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)

  // Untested -- no ESP32 port exists yet. This arm is written so that
  // adding one is a board backend plus a build, not a hunt through
  // src/cpu and src/machines. IRAM_ATTR is ESP-IDF's equivalent of keeping
  // a function out of memory-mapped flash, and esp_timer_get_time() is
  // documented as safe to call from an ISR.
  #include <esp_attr.h>
  #include <esp_timer.h>

  #define ARCADE_FAST_FUNC(name)     IRAM_ATTR name
  #define ARCADE_FAST_SECTION(tag)   IRAM_ATTR
  #define ARCADE_ISR_TIME_US()    ((uint32_t)esp_timer_get_time())
  #define ARCADE_TIME_US64()      ((uint64_t)esp_timer_get_time())

#else

  // Host builds (extras/tools/*_host) and any platform without a fast-RAM
  // concept. Placement becomes a no-op, which is correct: a laptop has no
  // XIP stall to avoid. Arduino's micros() is 32-bit, so the 64-bit form is
  // widened here rather than pretending the wrap does not exist.
  #define ARCADE_FAST_FUNC(name)     name
  #define ARCADE_FAST_SECTION(tag)

  #ifdef ARDUINO
    #include <Arduino.h>
    #define ARCADE_ISR_TIME_US()  ((uint32_t)micros())
    #define ARCADE_TIME_US64()    arcade_widen_micros_()
    static inline uint64_t arcade_widen_micros_(void) {
        static uint32_t last = 0;
        static uint64_t high = 0;
        uint32_t now = (uint32_t)micros();
        if (now < last) high += (uint64_t)1 << 32; // wrapped
        last = now;
        return high + now;
    }
  #else
    #include <time.h>
    static inline uint64_t arcade_host_us_(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)(ts.tv_nsec / 1000);
    }
    #define ARCADE_ISR_TIME_US()  ((uint32_t)arcade_host_us_())
    #define ARCADE_TIME_US64()    arcade_host_us_()
  #endif

#endif

#endif // ARCADE_PORTABILITY_H
