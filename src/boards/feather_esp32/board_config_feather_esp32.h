// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Adafruit Feather ESP32 V2 (#5400) + 2.4" TFT FeatherWing (#3315)
// + I2S 3W Class-D amp, MAX98357A (#3006), its Vin on 3V.
//
// BOTH REVISIONS OF THE WING WORK, and #3315 has shipped as V2 since
// Adafruit redesigned it on 2023-10-11 -- so the part number above buys a
// V2 today even though this port was brought up on a V1. Everything this
// file names is common to the two: same ILI9341, same microSD slot, same
// CS/DC/SD pins. The single difference that reaches the code is the touch
// controller, and it is handled at FEATHER_TOUCH_CS_IRQ below.
//
// On ESP32 the Arduino pin number IS the GPIO number.
#ifndef BOARD_CONFIG_FEATHER_ESP32_H
#define BOARD_CONFIG_FEATHER_ESP32_H

// --- TFT FeatherWing: fixed by the wing, not reassignable ------------------
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
// Touch, which this port does not use -- but the pin cannot simply be
// ignored, and WHAT IT IS depends on which revision of the wing is fitted:
//
//   V1   STMPE610 chip select, an INPUT to a device on this same SPI bus.
//        Left floating it can select itself and drive MISO against the SD
//        card, so it has to be held deasserted-high.
//   V2   TSC2007 PENIRQ, an open-drain OUTPUT (the TSC2007 is on I2C, so
//        there is no chip select at all). It idles high through the part's
//        own internal pull-up -- 50K by default, 90K if asked -- and is
//        pulled LOW by the controller whenever the screen is touched.
//
// The V2 half was MEASURED on hardware, not taken from the datasheet: with
// the pin read as plain INPUT it still rests HIGH (so the pull-up is out
// there on the wing, not in the ESP32) and it follows a finger on the glass
// LOW/HIGH in both INPUT and INPUT_PULLUP. DEVNOTES #124.
//
// INPUT_PULLUP is the one setting that is correct on both. It holds V1's
// CS high through the ESP32's ~45K (nothing else drives that line, and the
// traces are centimetres), while on V2 it is simply a second pull-up on a
// pin that already has one -- a touch then sinks about 70uA and nothing
// contends.
//
// DO NOT GO BACK TO OUTPUT/HIGH. That was the original code and it is
// correct only on V1: on a V2 wing it drives the pad hard high into the
// TSC2007's pull-down FET every time a finger lands on the screen, which
// is a short between two active drivers on a panel the player is sitting
// in front of. The V2 wing puts a cuttable solder jumper on this line, so
// a board that has had it cut leaves the pin floating -- INPUT_PULLUP is
// right there too.
#define FEATHER_TOUCH_CS_IRQ 32

// --- MAX98357A (mono #3006 by default; the stereo pair #6513 also works) ---
// Vin belongs on 3V: it is the only rail of the three that is right however
// the board is powered. VBUS is dead on battery and BAT is audibly crunchy
// with no cell fitted (the cell is the amp's bulk capacitance, DEVNOTES
// #125). BAT is the better choice once a battery IS fitted. See
// hal_audio_feather_esp32.cpp for the full matrix.
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
