// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Architecture dispatcher. Everything in src/cpu/ and src/machines/ that
// needs something chip-specific asks for it through here.
//
//     #include "arch/arch.h"
//
// (Path-qualified, because only src/ itself is on the include path -- not
// its subdirectories. See PORTING.md.)
//
// TWO LAYERS, AND THE DISTINCTION MATTERS:
//
//   arch   the silicon. Fast-RAM placement, timer registers, PIO, DMA.
//          Shared by every board built on that chip family.
//   board  the product. Which pins, which DAC and its I2C address, which
//          display. src/boards/<board>/.
//
// A Fruit Jam and a Feather RP2350 are two boards on one arch; an ESP32
// board is a different arch entirely. Keeping them apart is what makes the
// second of either cheap.
//
// WHY THESE ARE HEADERS AND NOT .c FILES -- this is a hard constraint, not
// a preference. Arduino compiles EVERY source file under src/ regardless of
// which board is selected; there is no per-architecture source selection. So
// each arm below is a header that guards its own contents and expands to
// nothing when it does not match. Adafruit_Protomatter's src/arch/ works
// exactly this way (14 headers, zero sources) and is the precedent followed
// here. Arch code that must live in a .cpp has to wrap the whole file in the
// same guard.
//
// ADDING AN ARCHITECTURE: write src/arch/<name>.h implementing the contract
// below, guard it, and add one #include here. Nothing in src/cpu/ or
// src/machines/ should need to change -- if it does, the contract is wrong
// and is the thing to fix.
//
// THE CONTRACT each arm must provide:
//
//   ARCADE_FAST_FUNC(name)    Place a function where it runs without a
//                             flash stall, in the NAME position:
//                                 void ARCADE_FAST_FUNC(fill)(int32_t*, int)
//                             MUST emit a separate linker section per
//                             function: every example compiles all seven
//                             machines and relies on --gc-sections to
//                             discard the six it does not use. One shared
//                             section defeats that and pulls every game's
//                             audio into every binary.
//
//   ARCADE_FAST_SECTION(tag)  The same placement in ATTRIBUTE position, for
//                             code that declares and defines separately:
//                                 static void step(void) ARCADE_FAST_SECTION("z80");
//                             `tag` groups one module's hot functions.
//
//   ARCADE_ISR_TIME_US()      32-bit microseconds, readable FROM AN ISR.
//                             Strict: an inline register or counter read,
//                             never a function call that might live in
//                             flash. Wraps ~71 minutes; unsigned
//                             subtraction handles it.
//
//   ARCADE_TIME_US64()        Monotonic 64-bit microseconds. Normal paths
//                             only, may be a real call. 64-bit because a
//                             caller derives emulated cycles from elapsed
//                             time and must not wrap mid-session.
#ifndef ARCADE_ARCH_H
#define ARCADE_ARCH_H

#include <stdint.h>

// Every arm is included unconditionally; each selects itself.
#include "arch/rp2040.h"
#include "arch/esp32.h"

// No arm matched. Host builds (extras/tools/*_host) land here and are
// expected to: a laptop has no XIP stall to avoid and no ISR to be safe
// inside, so placement becomes a no-op and time comes from the OS.
#ifndef ARCADE_FAST_FUNC

  #define ARCADE_FAST_FUNC(name)     name
  #define ARCADE_FAST_SECTION(tag)

  #ifdef ARDUINO
    #include <Arduino.h>
    static inline uint64_t arcade_widen_micros_(void) {
        // micros() is 32-bit; widen rather than pretend the wrap is not
        // there. Not ISR-safe (the statics are unguarded), which is fine --
        // ARCADE_TIME_US64() never promised that.
        static uint32_t last = 0;
        static uint64_t high = 0;
        uint32_t now = (uint32_t)micros();
        if (now < last) high += (uint64_t)1 << 32;
        last = now;
        return high + now;
    }
    #define ARCADE_ISR_TIME_US()     ((uint32_t)micros())
    #define ARCADE_TIME_US64()       arcade_widen_micros_()
  #else
    #include <time.h>
    static inline uint64_t arcade_host_us_(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)(ts.tv_nsec / 1000);
    }
    #define ARCADE_ISR_TIME_US()     ((uint32_t)arcade_host_us_())
    #define ARCADE_TIME_US64()       arcade_host_us_()
  #endif

#endif // no arch arm matched

#endif // ARCADE_ARCH_H
