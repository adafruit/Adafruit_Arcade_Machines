// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// hal_audio.h implementation for the Adafruit Feather ESP32 V2 with a
// Stereo I2S 3W amp, dual MAX98357A (Adafruit #6513).
//
// THIS FILE IS ALMOST EMPTY, AND THAT IS THE POINT. Compare
// boards/fruitjam/hal_audio_fruitjam.cpp, which spends sixty lines
// configuring a TLV320DAC3100 over I2C -- PLL, clock dividers, routing,
// gains, headset detect. The MAX98357A has no register interface at all:
// it is a Class-D amplifier with an I2S decoder on the front, and it works
// the instant clocks and data arrive. So the split that file describes
// still holds, one side of it is just empty here:
//
//   this file        the amp. Which pins, and nothing else, because there
//                    is nothing else to say.
//   src/arch/esp32/  the transport. I2S clocks and the task that feeds
//                    them, on the core's own ESP_I2S library.
//
// Wiring, from the board's silkscreen: BCLK to IO27, LRC to IO12, DIN to
// IO13, Vin to VBUS, GND to GND. Vin on VBUS gives the amp the full 5V,
// which is what the 3W rating assumes; on 3.3V it simply plays quieter.
//
// TWO PIN HAZARDS ON THIS BOARD, both recorded in board_config:
//
//  - GPIO 12 (LRC) is a STRAPPING PIN, MTDI. It must read LOW at reset or
//    the chip selects a 1.8V flash voltage and does not boot. The
//    MAX98357A presents an input here and does not drive it, so the board
//    boots on the pin's own pulldown -- but if this board ever stops
//    booting after an audio wiring change, look here first.
//  - GPIO 13 (DIN) also drives the onboard red LED. It will flicker in
//    time with the audio. That is cosmetic and expected, not a fault.
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32_V2)

#include "hal/arcade_hal_audio.h"
#include "board_config_feather_esp32.h"
#include "arch/esp32/arch_audio_i2s.h"

bool hal_audio_init(uint32_t sample_rate) {
    // No codec to bring up, so unlike the Fruit Jam there is no "did the
    // chip answer" half to this. Either I2S started or it did not.
    return arch_i2s_init(sample_rate,
                         FEATHER_I2S_BCLK, FEATHER_I2S_LRC, FEATHER_I2S_DIN);
}

void hal_audio_set_fill_callback(hal_audio_fill_cb cb) {
    arch_i2s_set_fill_callback(cb);
}

uint32_t hal_audio_enter_critical(void) {
    return arch_i2s_enter_critical();
}

void hal_audio_exit_critical(uint32_t saved_state) {
    arch_i2s_exit_critical(saved_state);
}

#endif // ARDUINO_ADAFRUIT_FEATHER_ESP32_V2
