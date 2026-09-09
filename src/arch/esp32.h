// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// ESP32 arm of the architecture contract. See arch/arch.h.
//
// UNTESTED. No ESP32 port exists yet and nothing here has ever been
// compiled, let alone run. It is written so that the first person to try
// starts from a stated contract rather than an archaeology exercise --
// treat every line as a hypothesis.
//
// What a real ESP32 port still needs beyond this file, so nobody mistakes
// this for "ESP32 is supported":
//
//   - A board backend under src/boards/. That is the hard part. The Fruit
//     Jam drives DVI over the RP2350's HSTX with the second core parked in
//     hal_video_run() forever; an ESP32 has no equivalent, so its video
//     backend is a new implementation against (say) an SPI LCD. The HAL is
//     shaped for it -- acquire_scanline/submit_scanline says nothing about
//     DVI -- but it is a port, not a recompile.
//   - An I2S path for audio. src/boards/fruitjam's is an RP2 PIO program.
//   - architectures= in library.properties widened past rp2040.
//   - Verification that IRAM_ATTR placement leaves enough IRAM: this
//     library puts CPU interpreters and audio mixers in fast memory, and
//     ESP32 IRAM is far scarcer than RP2350 SRAM.
#ifndef ARCADE_ARCH_ESP32_H
#define ARCADE_ARCH_ESP32_H

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)

#include <esp_attr.h>
#include <esp_timer.h>

// IRAM_ATTR is ESP-IDF's "do not execute this from memory-mapped flash".
// NOTE, and this is the one to check first: IRAM_ATTR does NOT give a
// separate section per function the way __not_in_flash_func() does, so
// --gc-sections may not discard unused machines' hot code. If a build pulls
// in all seven games' audio, this is why.
#define ARCADE_FAST_FUNC(name)     IRAM_ATTR name
#define ARCADE_FAST_SECTION(tag)   IRAM_ATTR

// esp_timer_get_time() is documented ISR-safe and is placed in IRAM by
// ESP-IDF, which is what the ISR contract requires. Verify that on the
// actual target before trusting it -- the RP2 equivalent looked inline and
// was not.
#define ARCADE_ISR_TIME_US()       ((uint32_t)esp_timer_get_time())
#define ARCADE_TIME_US64()         ((uint64_t)esp_timer_get_time())

#endif // ESP32
#endif // ARCADE_ARCH_ESP32_H
