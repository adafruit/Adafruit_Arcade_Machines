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
#include "driver/gpio.h"   // GPIO_IS_VALID_OUTPUT_GPIO
#include "hal/arcade_hal_input.h"
#include "board_config_feather_esp32.h"
#include "wii_input_feather_esp32.h"

// -1 marks a control this board does not wire. HAL_BTN_MIRROR,
// HAL_BTN_STRETCH, HAL_BTN_ACTION2 and HAL_BTN_ACTION3 read false forever,
// which is why the game sketches can call them unchanged.
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
    [HAL_BTN_ACTION2]= -1,
    [HAL_BTN_ACTION3]= -1,
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

// The core the input task (and the Wii controller's I2C polling) is pinned
// to: 0 unless a sketch's build_opt.h says otherwise. Galaga moves it to
// core 1 (DEVNOTES #146).
#ifndef FEATHER_INPUT_CORE
#define FEATHER_INPUT_CORE 0
#endif
#define POLL_INTERVAL_MS 1

typedef struct {
    bool     stable;
    bool     pending;
    uint32_t pending_since;
} debounce_t;

static debounce_t   s_filt[HAL_BTN_COUNT];
static volatile uint32_t s_state = 0;   // bit i = button i currently pressed

// THE GPIO BUTTONS ARE IGNORED WHILE A CONTROLLER IS CONNECTED (DEVNOTES
// #161). START2, LEFT, RIGHT and ROTATE are input-only pads with no internal
// pull-up (see hal_input_init() below): on a Feather without the button
// board they float, and read as random presses -- a phantom RIGHT, or a
// ROTATE that the settings file then saves. So with a Wii Classic / SNES
// Classic controller plugged in, the controller is the only input.
//
// Both directions wait PAD_SETTLE_MS, so a controller briefly not answering
// (the driver's `drops`) never lets the floating pins through. At boot the
// buttons start OFF and are decided once the Wii driver has had
// BOOT_DECIDE_MS -- two of its once-a-second tries at finding a controller,
// one just powered on can miss the first -- or NO_WII_FALLBACK_MS from boot
// in a sketch that never starts the driver.
#define PAD_SETTLE_MS      1000u
#define BOOT_DECIDE_MS     2500u
#define NO_WII_FALLBACK_MS 8000u
static volatile bool s_gpio_on = false;

// VOLUME ON THE GPIO PANEL: hold ROTATE, press Up or Down (DEVNOTES #161),
// as X + Up/Down does on the controller. ROTATE is a system button, not a
// game control, so it can be held back from the game at no cost: while it
// is held, Up and Down go to the volume and not to the game, and the game
// sees nothing. Let go without having touched Up or Down and the game gets
// a ROTATE press then, ROTATE_PULSE_MS long -- rotation happens on release.
#define ROTATE_PULSE_MS 100u

static uint32_t rotate_combo(uint32_t st, uint32_t now_ms) {
    static bool     held = false, used = false, prev_up = false, prev_down = false;
    static bool     pulsing = false;
    static uint32_t pulse_from = 0;
    const uint32_t rot = 1u << HAL_BTN_ROTATE, up = 1u << HAL_BTN_UP, down = 1u << HAL_BTN_DOWN;
    if (st & rot) {
        const bool u = (st & up) != 0, d = (st & down) != 0;
        if (!held) {
            held = true; used = false;
            prev_up = u; prev_down = d;   // already down when ROTATE went down: not a step
        }
        if (u && !prev_up)   { feather_wii_input_add_volume_steps(+1); used = true; }
        if (d && !prev_down) { feather_wii_input_add_volume_steps(-1); used = true; }
        prev_up = u; prev_down = d;
        st &= ~(rot | up | down);
    } else if (held) {
        held = false;
        if (!used) { pulsing = true; pulse_from = now_ms; }
    }
    if (pulsing) {
        if (now_ms - pulse_from < ROTATE_PULSE_MS) st |= rot;
        else pulsing = false;
    }
    return st;
}

static void gpio_set(bool on) {
    if (on) {
        // Start clean: nothing carried over from before the gap.
        for (int i = 0; i < HAL_BTN_COUNT; i++) {
            s_filt[i].stable = false;
            s_filt[i].pending = false;
        }
    }
    s_gpio_on = on;
    Serial.println(on ? "[input] GPIO buttons ON (no controller connected)"
                      : "[input] GPIO buttons OFF (a controller is connected)");
}

static void gpio_gate(uint32_t now_ms) {
    static bool     decided = false, last_pad = false, have_boot = false, have_wii = false;
    static uint32_t boot_ms = 0, wii_ms = 0, since = 0;
    // Real flags, not a nonzero time: a start time nudged to be nonzero
    // can land a millisecond ahead of `now`, and now - start then wraps to
    // a huge value -- which read as "timed out" on the very first call.
    if (!have_boot) { have_boot = true; boot_ms = now_ms; }
    const bool started = feather_wii_input_started();
    const bool pad = feather_wii_input_connected();
    if (started && !have_wii) { have_wii = true; wii_ms = now_ms; }
    if (!decided) {
        const bool timed_out = have_wii ? (now_ms - wii_ms >= BOOT_DECIDE_MS)
                                        : (now_ms - boot_ms >= NO_WII_FALLBACK_MS);
        if (!pad && !timed_out) return;
        decided = true;
        last_pad = pad;
        since = now_ms;
        gpio_set(!pad);
        return;
    }
    if (pad != last_pad) { last_pad = pad; since = now_ms; }
    if (now_ms - since < PAD_SETTLE_MS) return;
    if (pad == s_gpio_on) gpio_set(!pad);
}

// Pinned to core 0, away from the emulator and video on core 1. At 1kHz the
// task spends almost all its time blocked, so it costs nothing measurable.
static void input_task(void *arg) {
    (void)arg;
    for (;;) {
        const uint32_t now = micros();
        uint32_t st = 0;
        for (int i = 0; s_gpio_on && i < HAL_BTN_COUNT; i++) {
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
        s_state = s_gpio_on ? rotate_combo(st, millis()) : 0u;
        // A Wii Classic / SNES Classic controller, if the sketch started one
        // (wii_input_feather_esp32.h); returns at once otherwise.
        feather_wii_input_tick(millis());
        gpio_gate(millis());
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

// GPIO 34/36/39 and 37 are input-only pads with NO internal pull resistors,
// so START2, LEFT, RIGHT and ROTATE each carry an external resistor to 3V3 --
// 10K on this build, though board_config has the range and 10K is only the
// convenient pick within it.
//
// ASK FOR THE PULL-UP ONLY WHERE THE PAD HAS ONE. Requesting INPUT_PULLUP
// on all nine was believed harmless -- the hardware does ignore it -- but
// it is not silent: the IDF logs an error per pad on every boot,
//
//     E (2086) gpio: gpio_pullup_en(85): GPIO number error
//               (input-only pad has no internal PU)
//
// four times, once each for those four buttons. That is noise in the one
// place a bring-up log has to be trustworthy, and it is easy to read as a
// pin-numbering fault -- the (85) is gpio.c's __LINE__, not a GPIO number,
// which is exactly the misreading it invites.
//
// GPIO_IS_VALID_OUTPUT_GPIO() is the honest test rather than a hardcoded
// 34..39: on this part the input-only pads are precisely the ones that
// cannot drive an output, and the macro tracks that per SoC.
void hal_input_init(void) {
    for (int i = 0; i < HAL_BTN_COUNT; i++) {
        if (s_pin[i] >= 0)
            pinMode((uint8_t)s_pin[i],
                    GPIO_IS_VALID_OUTPUT_GPIO(s_pin[i]) ? INPUT_PULLUP : INPUT);
        s_filt[i].stable = false;
        s_filt[i].pending = false;
        s_filt[i].pending_since = 0;
    }
    static bool started = false;
    if (!started) {
        // Priority above the Arduino loop task so a long frame cannot delay
        // sampling -- the point of this task is a steady interval.
        xTaskCreatePinnedToCore(input_task, "arcade_input", 4096, NULL, 2, NULL,
                                FEATHER_INPUT_CORE);
        started = true;
    }
}

// A button is pressed if its GPIO line is, or if a connected Wii Classic /
// SNES Classic controller holds it (wii_input_feather_esp32.h). The
// controller needs no debounce here: it reports clean digital states.
bool hal_input_read(uint8_t index) {
    if (index >= HAL_BTN_COUNT) return false;
    return ((s_state >> index) & 1u) || feather_wii_input_held(index);
}
#endif
