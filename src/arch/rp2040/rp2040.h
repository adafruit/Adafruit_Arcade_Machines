// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// RP2040 / RP2350 arm of the architecture contract. See arch/arch.h for
// what these mean and why they are headers. Selects itself; expands to
// nothing on any other chip.
#ifndef ARCADE_ARCH_RP2040_H
#define ARCADE_ARCH_RP2040_H

#if defined(ARDUINO_ARCH_RP2040) || defined(PICO_ON_DEVICE)

#include "pico.h"
#include "pico/time.h"
#include "hardware/timer.h"

// __not_in_flash_func() emits a per-function .time_critical.<name> section,
// which is exactly the per-function granularity --gc-sections needs.
#define ARCADE_FAST_FUNC(name)     __not_in_flash_func(name)
#define ARCADE_FAST_SECTION(tag)   __attribute__((section(".time_critical." tag)))

// A direct MMIO load, which is the whole point. time_us_64() and
// time_us_32() are NOT inline here -- checked with `nm` on a linked .elf,
// both are separately compiled pico-sdk functions living in flash, and
// calling either from the audio ISR reintroduces the XIP stall that shows
// on screen as red scanlines. This project's own instrumentation did that
// once before it was caught. timer_hw->timerawl is what those functions
// read internally.
#define ARCADE_ISR_TIME_US()       (timer_hw->timerawl)
#define ARCADE_TIME_US64()         time_us_64()

#endif // RP2040/RP2350
#endif // ARCADE_ARCH_RP2040_H
