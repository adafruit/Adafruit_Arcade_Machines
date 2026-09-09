// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// hal_audio.h for the Feather ESP32 V2 -- SILENT STUB, deliberately.
//
// The dual MAX98357A needs an ESP32 I2S transport under src/arch/esp32/,
// and that has a real design question in it: on RP2 the fill callback runs
// in a DMA IRQ and hal_audio_enter_critical() just disables interrupts,
// whereas the natural ESP32 shape is a pinned FreeRTOS task, which makes
// the critical section a cross-core problem rather than an interrupt one.
// Getting that wrong produces intermittent audio corruption, which is a bad
// thing to be debugging at the same time as first-light video.
//
// So audio is stubbed until the display is trusted. The contract is
// satisfied: init succeeds, the callback is accepted and never invoked, and
// the critical section is a no-op because nothing else touches the state it
// guards. Games run silent.
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32_V2)
#include "hal/arcade_hal_audio.h"

bool hal_audio_init(uint32_t sample_rate) { (void)sample_rate; return true; }
void hal_audio_set_fill_callback(hal_audio_fill_cb cb) { (void)cb; }
uint32_t hal_audio_enter_critical(void) { return 0; }
void hal_audio_exit_critical(uint32_t saved_state) { (void)saved_state; }
#endif
