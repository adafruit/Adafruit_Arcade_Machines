// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// ESP32 I2S transmit, on the ESP32 core's own ESP_I2S library.
//
// This is ARCHITECTURE, not board: I2S is a silicon feature shared by every
// ESP32 board, and the only board facts are three pin numbers, which are
// passed in. The RP2 equivalent lives next door in arch/rp2040/ and is
// hand-written PIO, because nothing covers that part. Here something does,
// and after DEVNOTES #108/#109 -- where a hand-written SPI register
// sequence cost a great deal and the vendor driver simply worked -- the
// library is the default and hand-rolling would need a reason.
//
// (Adafruit_Zero_I2S is the obvious-sounding candidate and is NOT one: it
// is `architectures=samd`, for the Arduino Zero / M0 / M4. It will not
// compile for this chip.)
//
// THE SHAPE IS DIFFERENT FROM RP2, AND THAT IS THE INTERESTING PART.
// On RP2 the fill callback runs in a DMA completion IRQ and the critical
// section is "disable that IRQ". ESP_I2S offers a blocking write() with DMA
// behind it and no callback, so the natural form here is a FreeRTOS task --
// which makes the critical section a CROSS-CORE problem instead of an
// interrupt one. See the .cpp for how that is handled and why a spinlock is
// the right primitive rather than a mutex.
#ifndef ARCH_AUDIO_I2S_ESP32_H
#define ARCH_AUDIO_I2S_ESP32_H

#if defined(ARDUINO_ARCH_ESP32)

#include <stdint.h>
#include "hal/arcade_hal_audio.h" // hal_audio_fill_cb

#ifdef __cplusplus
extern "C" {
#endif

// Start I2S and the task that pumps it. All three pins are explicit --
// unlike the RP2 version, which infers word-select from the bit clock
// because its PIO program encodes them as adjacent GPIOs.
bool arch_i2s_init(uint32_t sample_rate, int bclk, int lrc, int din);

// The installed callback runs on the AUDIO TASK, not in an interrupt. It
// may block and it may touch flash; the constraint that governs the RP2
// version -- never stall, an XIP miss starves the video queue -- does not
// apply here, because this board's video has no queue to starve and the
// task is pinned away from the core running the game.
void arch_i2s_set_fill_callback(hal_audio_fill_cb cb);

// Guard state shared between the game and the audio task. Cross-core safe.
uint32_t arch_i2s_enter_critical(void);
void     arch_i2s_exit_critical(uint32_t saved_state);

#ifdef __cplusplus
}
#endif

#endif // ESP32
#endif // ARCH_AUDIO_I2S_ESP32_H
