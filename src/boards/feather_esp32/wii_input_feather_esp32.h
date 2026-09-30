// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// A Wii Classic or SNES Classic controller on the Feather ESP32 V2's STEMMA
// QT port, through the Wii Nunchuck breakout (extras/CONSOLES_PLAN.md, "I2C
// controllers"). The Feather's counterpart of the Fruit Jam's USB pads
// (boards/fruitjam/usb_input_fruitjam.h): the sketch picks a mapping once,
// and from then on hal_input_read() ORs the controller's buttons in with
// the Feather's own.
//
// READ ON THE INPUT TASK, not once per paint. hal_input_feather_esp32.cpp
// already samples the buttons on a 1 kHz task; every POLL_TICKS of those it
// sends the controller a read request, and it collects the report on the
// next tick, so the ~200 us the controller needs in between is never waited
// out. The controller is read at 250 Hz where a paint-rate read would be
// 30 Hz.
//
// The bus is the STEMMA QT one, SDA 22 / SCL 20, powered through GPIO 2
// (the core's NEOPIXEL_I2C_POWER), at 100 kHz (see the .cpp for why not 400). The TFT FeatherWing's touch
// controller (a TSC2007 at 0x48 on the V2 wing) shares it; the Wii
// controller answers at 0x52.
#ifndef WII_INPUT_FEATHER_ESP32_H
#define WII_INPUT_FEATHER_ESP32_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    // A=SHOOT, B=ACTION2, Start=START1, Select=COIN, Y=START2, R=ROTATE,
    // and X + Up/Down for the volume, as below.
    FEATHER_WII_MAP_ARCADE = 0,
    // The Game Boy and the NES: A=SHOOT, B=ACTION2, Start=START1,
    // Select=COIN, L=STRETCH, R=ROTATE, Y=MIRROR (the palette), so the
    // controller alone does everything the Fruit Jam's Buttons 1-3 do.
    // X is the VOLUME modifier: while it is held, Up and Down step the
    // volume (feather_wii_input_take_volume_steps()) and the D-pad is not
    // passed to the game.
    FEATHER_WII_MAP_CONSOLE,
} feather_wii_map_t;

// Powers the STEMMA QT port, starts the bus and picks the mapping. Call once
// in setup(), after hal_input_init() (the machines' cart or ROM load calls
// it). A controller can be plugged in or out at any time after.
void feather_wii_input_begin(feather_wii_map_t map);

// For hal_input_feather_esp32.cpp's input task: called on every tick.
void feather_wii_input_tick(uint32_t now_ms);

// Whether the controller holds this HAL_BTN_* (hal_input_read() ORs it in).
bool feather_wii_input_held(uint8_t hal_btn);

// Whether feather_wii_input_begin() has run, and whether a controller is
// connected and answering now. For the input task's GPIO gate
// (hal_input_feather_esp32.cpp); call from that task.
bool feather_wii_input_started(void);
bool feather_wii_input_connected(void);

// Adds volume steps from another source into the same count: the GPIO
// panel's ROTATE + Up/Down (hal_input_feather_esp32.cpp). Input task only.
void feather_wii_input_add_volume_steps(int steps);

// Volume steps since the last call, from X+Up (+1 each press) and X+Down
// (-1), in either map, and from the GPIO panel's ROTATE + Up/Down. Counted on the input task, so a quick tap between
// two paints still counts. The sketch applies them: the consoles through
// console_audio_volume_step(), the arcade games through
// feather_audio_volume_step() (hal_audio_feather_esp32.h).
int feather_wii_input_take_volume_steps(void);

// For the heartbeat.
typedef struct {
    bool     connected;
    bool     hires;
    uint16_t buttons;     // WII_BTN_* held
    uint32_t connects;    // times a controller was found since begin()
    uint32_t drops;       // times a connected controller stopped answering
    uint32_t request_fails, read_fails; // report transactions that failed
    uint8_t  last_fail_step, last_fail_code; // wii_classic_t's diagnostics
} feather_wii_input_stats_t;
void feather_wii_input_get_stats(feather_wii_input_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
