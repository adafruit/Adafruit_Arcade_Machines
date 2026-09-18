// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// hal_audio.h implementation for the Adafruit Feather ESP32 V2 with an
// I2S 3W Class-D amp, MAX98357A (Adafruit #3006).
//
// THE MONO BREAKOUT IS THE DEFAULT AND THE STEREO PAIR (#6513) ALSO WORKS,
// unchanged, because there is nothing stereo to lose: hal_audio.h's fill
// callback packs one mono mix into both channels as
// (sample << 16) | (uint16_t)sample, and the MAX98357A's SD_MODE pin
// selects (L+R)/2 by default. Averaging two identical channels returns the
// same signal. One speaker is also the obvious choice for a cabinet.
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
// IO13, GND to GND, and **Vin to 3V**.
//
// VIN IS A REAL CHOICE AND 3V IS THE ONLY ONE THAT IS ALWAYS RIGHT. All
// three rails on the Feather header drive this amp -- it runs on 2.5-5.5V
// -- and the other two each have a power configuration they fail in:
//
//                   USB, no battery   battery only   USB + battery
//   VBUS            loudest           DEAD           loudest
//   3V              fine              fine           fine
//   BAT             CRUNCHY           great          great
//
//   VBUS  the USB 5V rail, and the loudest: 5V is what the 3W rating
//         assumes. It is also dead on battery. A board wired this way plays
//         perfectly on the bench and goes silent the moment it is
//         unplugged, with picture and input still working.
//   3V    the regulated rail. Quietest -- power goes as V^2, so 3.3V
//         against 5V is about 44%, roughly -3.5dB -- and it puts the amp on
//         the same LDO as the ESP32 and the panel, which in principle lets
//         a Class-D amp's transients droop a rail the rest of the board
//         depends on. That has NOT been observed here. What recommends it
//         is the middle row above: there is no way to power this board that
//         it gets wrong.
//   BAT   3.7-4.2V, most of VBUS's volume, and upstream of the LDO so the
//         amp's transients cannot reach the ESP32 or the display. **The
//         best option WHEN A CELL IS ACTUALLY FITTED, and audibly bad when
//         one is not.** See below -- this is the one that was found the
//         hard way.
//
// WITHOUT A BATTERY, BAT IS NOT A BATTERY RAIL -- it is the LiPo charger's
// output, and a linear charger is a poor supply: a current limit of a few
// hundred mA and, decisively, NO RESERVOIR. A Class-D amp draws in bursts
// tracking the audio waveform, peaking into the hundreds of mA, and what
// normally absorbs those is the cell itself -- a LiPo is an enormous, very
// low-impedance capacitor sitting directly on that pin. Remove it and the
// rail sags and recovers at audio rate, modulating the amp's own supply.
// It sounds crunchy and distorted, and it is the supply, not the mixer.
//
// A 470uF-1000uF bulk cap at the amp's Vin does the reservoir job the
// missing cell was doing, if BAT is wanted anyway. DEVNOTES #125.

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
