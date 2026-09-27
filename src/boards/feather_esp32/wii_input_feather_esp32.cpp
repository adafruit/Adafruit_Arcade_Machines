// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See wii_input_feather_esp32.h.
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32_V2)
#include <Arduino.h>
#include <Wire.h>
#include "wii_input_feather_esp32.h"
#include "hal/arcade_hal_input.h"
#include "board_config_feather_esp32.h"   // HAL_BTN_*
#include "input/wii_classic.h"

// 100 kHz, NOT the 400 kHz the Fruit Jam's self-test ran this controller
// at. On this bus (shared with the TFT FeatherWing's touch controller) a
// first-party Wii Classic at 400 kHz passed start-up and its identity read,
// then returned every report as all 0xFF, with an occasional NACK. At
// 100 kHz the reports are real and nothing failed (DEVNOTES #143). A poll is
// then ~1 ms of bus time, spent blocked in the I2C driver, not computing.
#ifndef WII_I2C_HZ
#define WII_I2C_HZ  100000u
#endif
// One read every POLL_TICKS input-task ticks (1 ms each): the request on
// the first tick, the collect on the next.
#define POLL_TICKS  4u

static wii_classic_t      s_pad;
static feather_wii_map_t  s_map = FEATHER_WII_MAP_ARCADE;
static volatile bool      s_active = false;   // begin() has run
static volatile uint32_t  s_held = 0;         // bit i = HAL_BTN_i held
static volatile int32_t   s_volume_steps = 0;
static uint32_t           s_tick = 0;
static uint16_t           s_prev = 0;

// WII_BTN_* -> HAL_BTN_* for everything but the D-pad and the volume
// modifier, which map_buttons() handles itself.
typedef struct { uint16_t wii; uint8_t hal; } pair_t;

static const pair_t kArcade[] = {
    { WII_BTN_A,     HAL_BTN_SHOOT   },
    { WII_BTN_B,     HAL_BTN_ACTION2 },
    { WII_BTN_PLUS,  HAL_BTN_START1  },
    { WII_BTN_MINUS, HAL_BTN_COIN    },
    { WII_BTN_Y,     HAL_BTN_START2  },
    { WII_BTN_R,     HAL_BTN_ROTATE  },
};
static const pair_t kConsole[] = {
    { WII_BTN_A,     HAL_BTN_SHOOT   },
    { WII_BTN_B,     HAL_BTN_ACTION2 },
    { WII_BTN_PLUS,  HAL_BTN_START1  },
    { WII_BTN_MINUS, HAL_BTN_COIN    },
    { WII_BTN_L,     HAL_BTN_STRETCH },
    { WII_BTN_R,     HAL_BTN_ROTATE  },
    { WII_BTN_Y,     HAL_BTN_MIRROR  },
};

static uint32_t map_buttons(uint16_t w) {
    const bool console = (s_map == FEATHER_WII_MAP_CONSOLE);
    const pair_t *t = console ? kConsole : kArcade;
    const size_t n = console ? sizeof kConsole / sizeof kConsole[0]
                             : sizeof kArcade / sizeof kArcade[0];
    uint32_t held = 0;
    for (size_t i = 0; i < n; i++)
        if (w & t[i].wii) held |= 1u << t[i].hal;

    // The D-pad, unless X is holding it for the volume.
    if (!(console && (w & WII_BTN_X))) {
        if (w & WII_BTN_UP)    held |= 1u << HAL_BTN_UP;
        if (w & WII_BTN_DOWN)  held |= 1u << HAL_BTN_DOWN;
        if (w & WII_BTN_LEFT)  held |= 1u << HAL_BTN_LEFT;
        if (w & WII_BTN_RIGHT) held |= 1u << HAL_BTN_RIGHT;
    }
    return held;
}

void feather_wii_input_begin(feather_wii_map_t map) {
    s_map = map;
    // The STEMMA QT port's power is switched, and off at reset.
    pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
    Wire.begin(SDA, SCL, WII_I2C_HZ);
    wii_classic_begin(&s_pad, &Wire);
    s_active = true;
}

void feather_wii_input_tick(uint32_t now_ms) {
    if (!s_active) return;
    const uint32_t phase = s_tick++ % POLL_TICKS;
    if (phase == 0) {
        wii_classic_service(&s_pad, now_ms); // finds (and re-finds) a controller
        wii_classic_request(&s_pad);
    } else if (phase == 1) {
        wii_classic_collect(&s_pad);
        const uint16_t w = wii_classic_buttons(&s_pad);
        if (s_map == FEATHER_WII_MAP_CONSOLE && (w & WII_BTN_X)) {
            const uint16_t pressed = w & (uint16_t)~s_prev;
            if (pressed & WII_BTN_UP)   s_volume_steps = s_volume_steps + 1;
            if (pressed & WII_BTN_DOWN) s_volume_steps = s_volume_steps - 1;
        }
        s_prev = w;
        s_held = map_buttons(w);
    }
}

bool feather_wii_input_held(uint8_t hal_btn) {
    return hal_btn < 32 && ((s_held >> hal_btn) & 1u);
}

int feather_wii_input_take_volume_steps(void) {
    // Read-and-clear across tasks; a step landing between the two lines is
    // kept for the next call.
    const int32_t v = s_volume_steps;
    s_volume_steps = s_volume_steps - v;
    return (int)v;
}

void feather_wii_input_get_stats(feather_wii_input_stats_t *out) {
    out->connected = s_pad.state == WII_STATE_READY;
    out->hires     = s_pad.hires;
    out->buttons   = wii_classic_buttons(&s_pad);
    out->connects  = s_pad.connects;
    out->drops     = s_pad.drops;
    out->request_fails  = s_pad.step_fails[5];
    out->read_fails     = s_pad.step_fails[6];
    out->last_fail_step = s_pad.last_fail_step;
    out->last_fail_code = s_pad.last_fail_code;
}
#endif
