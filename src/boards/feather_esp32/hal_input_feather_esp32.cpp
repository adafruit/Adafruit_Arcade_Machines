// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// hal_input.h for the Feather ESP32 V2. Nine buttons, active low.
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32_V2)
#include <Arduino.h>
#include "hal/arcade_hal_input.h"
#include "board_config_feather_esp32.h"

// -1 marks a control this board does not wire. HAL_BTN_MIRROR and
// HAL_BTN_STRETCH read false forever, which is why the game sketches can
// call them unchanged.
static const int8_t s_pin[HAL_BTN_COUNT] = {
    [HAL_BTN_ROTATE] = FEATHER_BTN_ROTATE,
    [HAL_BTN_MIRROR] = -1,
    [HAL_BTN_COIN]   = FEATHER_BTN_COIN,
    [HAL_BTN_START1] = FEATHER_BTN_START1,
    [HAL_BTN_START2] = FEATHER_BTN_START2,
    [HAL_BTN_LEFT]   = FEATHER_BTN_LEFT,
    [HAL_BTN_RIGHT]  = FEATHER_BTN_RIGHT,
    [HAL_BTN_SHOOT]  = FEATHER_BTN_SHOOT,
    [HAL_BTN_UP]     = FEATHER_BTN_UP,
    [HAL_BTN_DOWN]   = FEATHER_BTN_DOWN,
    [HAL_BTN_STRETCH]= -1,
};

// GPIO 34/36/39 and 37 are input-only with NO internal pull resistors --
// INPUT_PULLUP silently does nothing on them, so each carries an external
// 10K to 3V3. Requesting INPUT_PULLUP anyway is harmless and keeps the
// table uniform; on those four it is simply ignored by the hardware.
void hal_input_init(void) {
    for (int i = 0; i < HAL_BTN_COUNT; i++) {
        if (s_pin[i] >= 0) pinMode((uint8_t)s_pin[i], INPUT_PULLUP);
    }
}

bool hal_input_read(uint8_t index) {
    if (index >= HAL_BTN_COUNT) return false;
    int8_t p = s_pin[index];
    if (p < 0) return false;               // not wired on this board
    return digitalRead((uint8_t)p) == LOW; // active low
}
#endif
