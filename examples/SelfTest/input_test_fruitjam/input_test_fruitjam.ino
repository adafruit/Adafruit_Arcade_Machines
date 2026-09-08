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
// Expected result: pressing/releasing each of the 8 buttons (Button 2 =
// rotate, Button 3 = mirror, plus coin/start1/start2/left/right/shoot on
// the header pins) prints a PRESSED/released line over Serial (115200
// baud) for that button and no others -- if a press shows up under the
// wrong name, double-check the physical wiring against
// board_config_fruitjam.h's HAL_BTN_* pin table.
#include <Adafruit_Arcade_Machines.h>
#include <hal/arcade_hal_input.h>
#include <boards/fruitjam/board_config_fruitjam.h>

static const char *names[] = {
    "ROTATE (B2)", "MIRROR (B3)", "COIN", "START1", "START2", "LEFT", "RIGHT", "SHOOT"
};

static bool last[8] = {false, false, false, false, false, false, false, false};

void setup() {
    Serial.begin(115200);
    delay(500);
    hal_input_init();
    Serial.println("Input smoke test -- press buttons to see state changes.");
}

void loop() {
    for (uint8_t i = 0; i < HAL_INPUT_BUTTON_COUNT; i++) {
        bool now = hal_input_read(i);
        if (now != last[i]) {
            Serial.print(names[i]);
            Serial.println(now ? ": PRESSED" : ": released");
            last[i] = now;
        }
    }
    delay(20); // simple poll-rate debounce
}
