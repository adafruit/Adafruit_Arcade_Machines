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
// GPIO matrix. **40MHz IS THE CEILING ON THIS WING AND IT HAS BEEN TESTED.**
// That is 5 MB/s, 30.7ms for a 320x240 RGB565 frame, and therefore a hard
// 32.6fps limit for any game that repaints the whole screen.
//
// DO NOT RAISE THIS. 80MHz runs -- 20.7ms/frame, 48fps, and the serial log
// looks like a straight win -- but the picture wiggles left and right, like
// a TV losing horizontal hold: the panel drops clock edges, so pixels shift
// within each row and the image walks. A frame-time counter cannot see it.
// See DEVNOTES #105.
//
// There is nothing between 40 and 80 to fall back to. The divider is
// 80MHz APB / ((pre+1) * (n+1)) with n+1 >= 2, so the only rungs are 80
// (a special equal-to-sysclk bit), 40, 26.7, 20 and down. Asking for 60 or
// 53 silently gets you 40. See _spiFrequencyToClockDivWithSource() in the
// core's esp32-hal-spi.c.
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

// --- Panel mounting -------------------------------------------------------
// HOW THIS BOARD IS HELD, expressed as a quarter-turn offset applied to
// whatever rotation a machine defaults to. Added by the sketch, not by the
// HAL -- the HAL has no notion of rotation, and a machine's default is the
// machine's business.
//
// WHY IT IS NOT ZERO, and why this is one number rather than a per-game
// table. Each machine's default encodes ITS CABINET's convention: the
// 8080bw games, Donkey Kong and Burger Time default to 1, the Namco games
// to 3, because the real cabinets mounted their monitors in opposite
// orientations. Those defaults are calibrated against the Fruit Jam's
// monitor orientation, and they are correct there. This board is held 180
// degrees round from that -- the FeatherWing is portrait with the USB and
// the button board where they physically need to be -- so every game needs
// the same half turn, and the per-game conventions still compose on top of
// it. Two is the value that turns Pac-Man's 3 into the 1 confirmed upright
// on this hardware.
//
// If a future game comes up upside down here, THIS is the number to
// question, not that machine's default. Changing a machine's default to fix
// one board breaks it on the other -- see the comment above
// pacman_machine.cpp's `system->rotation = 3`, which records exactly that
// mistake being made once already.
#define FEATHER_ROTATION_OFFSET 2

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
