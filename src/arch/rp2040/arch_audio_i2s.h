// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// RP2040/RP2350 I2S transmit: a PIO state machine fed by a chained pair of
// DMA channels, whose completion IRQ calls back for the next buffer.
//
// This is ARCHITECTURE, not board. PIO and the RP2 DMA controller are
// silicon features shared by every RP2 board; the only board facts are two
// pin numbers, which are passed in. It lived in src/boards/fruitjam/ until
// the arch split, where it read as Fruit Jam-specific code and was not.
//
// There is no library for this part. Adafruit_TLV320_I2S -- which the Fruit
// Jam backend now uses for the DAC itself -- is I2C configuration only and
// has no data path, so the transport stays hand-written. An ESP32 port
// needs its own equivalent under src/arch/esp32/.
#ifndef ARCH_AUDIO_I2S_RP2040_H
#define ARCH_AUDIO_I2S_RP2040_H

#if defined(ARDUINO_ARCH_RP2040) || defined(PICO_ON_DEVICE)

#include <stdint.h>
#include "hal/arcade_hal_audio.h" // hal_audio_fill_cb

#ifdef __cplusplus
extern "C" {
#endif

// Start the PIO state machine and the DMA pair. `pin_din` is the data line.
// `pin_bclk` is the bit clock; word-select is assumed to be the next GPIO up
// (pin_bclk + 1), which is what audio_i2s.pio encodes.
bool arch_i2s_init(uint32_t sample_rate, uint32_t pin_din, uint32_t pin_bclk);

// Installed callback runs in the DMA completion IRQ, from fast RAM. Nothing
// in it may stall: an XIP cache miss there is long enough to starve the
// video scanline queue, which shows on screen as coloured lines (see
// extras/DEVNOTES.md).
void arch_i2s_set_fill_callback(hal_audio_fill_cb cb);

// Block and restore the audio IRQ around state the fill callback also
// touches.
uint32_t arch_i2s_enter_critical(void);
void     arch_i2s_exit_critical(uint32_t saved_state);

#ifdef __cplusplus
}
#endif

#endif // RP2
#endif // ARCH_AUDIO_I2S_RP2040_H
