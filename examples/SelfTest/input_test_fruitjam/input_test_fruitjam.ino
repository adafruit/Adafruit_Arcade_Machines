// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// BEFORE YOU BUILD -- set Tools > Optimize to "Optimize More (-O2)".
//
// The sketch.yaml beside this file pins that level and `arduino-cli` reads
// it automatically; the Arduino IDE does not always, so set it by hand. The
// core's default is "Small (-Os) (standard)", which the games in this
// project are too slow at (../DEVNOTES.md #35 and #49); these standalone
// tests stay on the same setting as the sketches they are diagnosing.

// Standalone smoke test for ArcadeBoard_FruitJam's hal_input implementation.
//
// Prints each button's state whenever it changes -- no CPU emulator, no
// video, no audio, no SD card. Exercises the real production
// hal_input_fruitjam.cpp (raw GPIO + pull-ups) in isolation.
//
// Expected result: pressing/releasing each button (the on-board Buttons 1-3
// for STRETCH, ROTATE and MIRROR, plus every header-pin button in
// board_config_fruitjam.h, ACTION2 on A2 and ACTION3 on A1 included) prints
// a PRESSED/released line over Serial (115200 baud) for that button and no
// others -- if a press shows up under the wrong name, double-check the
// physical wiring against board_config_fruitjam.h's HAL_BTN_* pin table.
#include <Adafruit_Arcade_Machines.h>
#include <hal/arcade_hal_input.h>
#include <boards/fruitjam/board_config_fruitjam.h>

// Indexed by the HAL_BTN_* constants themselves, and read only through
// btn_name(), which returns "?" for anything past the end. This used to be
// a plain 8-entry list, with an 8-entry `last[]`, walked with the board's
// button count; the board grew to 11 buttons and neither array did, so the
// loop read names and WROTE state past their ends. A designated-initializer
// table can't drift out of order, and the bounds checks mean a button
// added later prints "?" instead of corrupting memory.
static const char *const names[] = {
    [HAL_BTN_ROTATE]  = "ROTATE (B2)",
    [HAL_BTN_MIRROR]  = "MIRROR (B3)",
    [HAL_BTN_COIN]    = "COIN (A5)",
    [HAL_BTN_START1]  = "START1 (D6)",
    [HAL_BTN_START2]  = "START2 (D7)",
    [HAL_BTN_LEFT]    = "LEFT (D8)",
    [HAL_BTN_RIGHT]   = "RIGHT (D9)",
    [HAL_BTN_SHOOT]   = "SHOOT (D10)",
    [HAL_BTN_UP]      = "UP (A3)",
    [HAL_BTN_DOWN]    = "DOWN (A4)",
    [HAL_BTN_STRETCH] = "STRETCH (B1)",
    [HAL_BTN_ACTION2] = "ACTION2 (A2)",
    [HAL_BTN_ACTION3] = "ACTION3 (A1)",
};

static const char *btn_name(uint8_t i) {
    if (i >= sizeof names / sizeof names[0] || !names[i]) return "?";
    return names[i];
}

static bool last[32]; // room to grow; the loop below also stops at its end

void setup() {
    Serial.begin(115200);
    delay(500);
    hal_input_init();
    Serial.print("Input smoke test -- ");
    Serial.print(HAL_INPUT_BUTTON_COUNT);
    Serial.println(" buttons. Press buttons to see state changes.");
}

void loop() {
    for (uint8_t i = 0; i < HAL_INPUT_BUTTON_COUNT && i < sizeof last; i++) {
        bool now = hal_input_read(i);
        if (now != last[i]) {
            Serial.print(btn_name(i));
            Serial.println(now ? ": PRESSED" : ": released");
            last[i] = now;
        }
    }
    delay(20); // simple poll-rate debounce
}
