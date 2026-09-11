// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// hal_input.h for the Feather ESP32 V2. Nine buttons, active low.
//
// SAMPLED ON ITS OWN 1kHz TASK, NOT ONCE PER FRAME, and that is the whole
// design. It looks like over-engineering next to the Fruit Jam's backend,
// which filters inside hal_input_read(); it is not, because the two boards
// call that function at very different rates.
//
// The Fruit Jam paints at a true 60Hz, so reading once per frame samples
// every 16.7ms and a 25ms release hold-off spans one to two frames. This
// board paints at 22-30fps depending on the game -- 33 to 44ms between
// samples -- and its display rate is a property of SPI bandwidth, not of
// anything a player can feel. Filtering at that granularity does nothing:
// a release hold shorter than the sampling interval is satisfied by the
// very next sample, and one shorter still cannot see bounce at all.
//
// The symptom is not subtle. Galaga fires one bullet per press edge, so a
// single tap seen as press-release-press gives TWO BULLETS -- which is
// exactly the failure DEVNOTES #32 chased into galaga_51xx.cpp, and exactly
// what that entry predicted would mean contacts rather than emulation if it
// ever came back with the 51XX fix in place. It came back on this board,
// and the cause is here: no filter, and a sampling interval three times
// what the Fruit Jam has.
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32_V2)
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

// ASYMMETRIC, matching the Fruit Jam's: a press is believed the instant it
// is seen, and only a RELEASE has to persist. Latency on the press edge
// would be felt immediately in a shooter; latency on the release edge only
// limits how fast a button can be re-triggered.
//
// 25ms is the same constant the Fruit Jam uses, and here it is genuinely 25
// samples rather than "however long until the next frame". See that file
// for why it is short, and why a long hold-off was removed as a workaround
// for a misdiagnosis -- Galaga is played by tapping, and 150ms caps that at
// about 5 shots/second.
#define RELEASE_HOLD_US 25000u
#define POLL_INTERVAL_MS 1

typedef struct {
    bool     stable;
    bool     pending;
    uint32_t pending_since;
} debounce_t;

static debounce_t   s_filt[HAL_BTN_COUNT];
static volatile uint32_t s_state = 0;   // bit i = button i currently pressed

// Pinned to core 0, away from the emulator and video on core 1. At 1kHz the
// task spends almost all its time blocked, so it costs nothing measurable.
static void input_task(void *arg) {
    (void)arg;
    for (;;) {
        const uint32_t now = micros();
        uint32_t st = 0;
        for (int i = 0; i < HAL_BTN_COUNT; i++) {
            if (s_pin[i] < 0) continue;
            const bool raw = digitalRead((uint8_t)s_pin[i]) == LOW;
            debounce_t *f = &s_filt[i];

            if (raw == f->stable) {
                f->pending = false;              // agrees; nothing pending
            } else if (raw) {
                f->stable  = true;               // press: believed at once
                f->pending = false;
            } else if (!f->pending) {
                f->pending = true;               // release: start the clock
                f->pending_since = now;
            } else if ((uint32_t)(now - f->pending_since) >= RELEASE_HOLD_US) {
                f->stable  = false;              // release: held long enough
                f->pending = false;
            }
            if (f->stable) st |= (1u << i);
        }
        s_state = st;
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

// GPIO 34/36/39 and 37 are input-only with NO internal pull resistors --
// INPUT_PULLUP silently does nothing on them, so each carries an external
// 10K to 3V3. Requesting INPUT_PULLUP anyway is harmless and keeps the
// table uniform; on those four it is simply ignored by the hardware.
void hal_input_init(void) {
    for (int i = 0; i < HAL_BTN_COUNT; i++) {
        if (s_pin[i] >= 0) pinMode((uint8_t)s_pin[i], INPUT_PULLUP);
        s_filt[i].stable = false;
        s_filt[i].pending = false;
        s_filt[i].pending_since = 0;
    }
    static bool started = false;
    if (!started) {
        // Priority above the Arduino loop task so a long frame cannot delay
        // sampling -- the point of this task is a steady interval.
        xTaskCreatePinnedToCore(input_task, "arcade_input", 2048, NULL, 2, NULL, 0);
        started = true;
    }
}

bool hal_input_read(uint8_t index) {
    if (index >= HAL_BTN_COUNT) return false;
    return (s_state >> index) & 1u;
}
#endif
