// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Adafruit Feather ESP32 V2 (#5400) + 2.4" TFT FeatherWing V1 (#3315)
// + Stereo I2S 3W amp, dual MAX98357A (#6513).
//
// On ESP32 the Arduino pin number IS the GPIO number.
#ifndef BOARD_CONFIG_FEATHER_ESP32_H
#define BOARD_CONFIG_FEATHER_ESP32_H

// --- TFT FeatherWing V1: fixed by the wing, not reassignable ---------------
// Shared hardware SPI: SCK 5, MOSI 19, MISO 21 (the variant's defaults).
// NOTE these are NOT the ESP32's IOMUX SPI pins, so the signals route via the
// GPIO matrix, which caps SPI at 40MHz. Measured ceiling on this wiring is
// 3.44 MB/s -- 22.4fps for a full 320x240 frame. Asking for 60MHz returns
// exactly what 40MHz does. That is a property of the wing, not a setting.
#define FEATHER_TFT_CS    15
#define FEATHER_TFT_DC    33
#define FEATHER_SD_CS     14
#define FEATHER_STMPE_CS  32  // touch, unused -- must be driven HIGH or the
                              // STMPE610 fights the SD card on MISO

// --- Dual MAX98357A --------------------------------------------------------
#define FEATHER_I2S_BCLK  27
#define FEATHER_I2S_LRC   12  // strapping pin (MTDI): must read LOW at boot
#define FEATHER_I2S_DIN   13  // shares the onboard red LED; it flickers on audio

// --- Buttons ---------------------------------------------------------------
// 34, 36, 39 and 37 are INPUT-ONLY with no internal pulls: external 10K to
// 3V3 required. The rest use INPUT_PULLUP.
#define FEATHER_BTN_COIN    26 // A0
#define FEATHER_BTN_START1  25 // A1
#define FEATHER_BTN_START2  34 // A2  external pull-up
#define FEATHER_BTN_LEFT    39 // A3  external pull-up
#define FEATHER_BTN_RIGHT   36 // A4  external pull-up
#define FEATHER_BTN_SHOOT    4 // A5
#define FEATHER_BTN_UP       7 // RX  (free on the PICO-V3 module)
#define FEATHER_BTN_DOWN     8 // TX
#define FEATHER_BTN_ROTATE  37 //     external pull-up

#ifdef __cplusplus
extern "C" {
#endif

// The full HAL_BTN_* set is defined even though this board only wires nine
// of them. The game sketches read HAL_BTN_MIRROR and HAL_BTN_STRETCH
// directly, so dropping the enumerators would fork every sketch; instead
// hal_input_read() returns false for the two with no pin. One board having
// fewer controls should not change what a game's composition root looks like.
enum {
    HAL_BTN_ROTATE = 0,
    HAL_BTN_MIRROR,     // not wired on this board -- always reads false
    HAL_BTN_COIN,
    HAL_BTN_START1,
    HAL_BTN_START2,
    HAL_BTN_LEFT,
    HAL_BTN_RIGHT,
    HAL_BTN_SHOOT,
    HAL_BTN_UP,
    HAL_BTN_DOWN,
    HAL_BTN_STRETCH,    // not wired on this board -- always reads false
    HAL_BTN_COUNT
};

#ifdef __cplusplus
}
#endif
#endif // BOARD_CONFIG_FEATHER_ESP32_H
