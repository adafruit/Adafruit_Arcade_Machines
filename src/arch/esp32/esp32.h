// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// ESP32 arm of the architecture contract. See arch/arch.h.
//
// Used by the Feather ESP32 V2 board (src/boards/feather_esp32): all seven
// arcade games since v2.10, and the Game Boy and NES (DEVNOTES #141/#142).
//
// IRAM IS THE SCARCE RESOURCE. ESP32 code runs from flash through a 32 KB
// cache unless it is placed in IRAM, about 128 KB in all, much of it taken
// by ESP-IDF itself. Place only code measured to need it: Peanut-GB's CPU
// loop there cut Tetris's frame by 12.5% (#142); nofrendo's, tried the
// same way, bought nothing (#140).
#ifndef ARCADE_ARCH_ESP32_H
#define ARCADE_ARCH_ESP32_H

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)

#include <esp_attr.h>
#include <esp_timer.h>

// IRAM_ATTR is ESP-IDF's "do not execute this from memory-mapped flash".
// It gives each function its own section, ".iram1.<__COUNTER__>"
// (esp_attr.h), so --gc-sections still discards unused machines' hot code,
// as the contract in arch.h requires.
#define ARCADE_FAST_FUNC(name)     IRAM_ATTR name
#define ARCADE_FAST_SECTION(tag)   IRAM_ATTR

// esp_timer_get_time() is documented ISR-safe and is placed in IRAM by
// ESP-IDF, which is what the ISR contract requires.
#define ARCADE_ISR_TIME_US()       ((uint32_t)esp_timer_get_time())
#define ARCADE_TIME_US64()         ((uint64_t)esp_timer_get_time())

#endif // ESP32
#endif // ARCADE_ARCH_ESP32_H
