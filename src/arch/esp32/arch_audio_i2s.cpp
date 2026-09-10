// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// ESP32 I2S transmit -- see arch_audio_i2s.h for what this is and why it
// uses the core's ESP_I2S library rather than raw registers.
//
// The whole file is guarded: Arduino compiles every source under src/
// regardless of the selected board, so an arch .cpp has to exclude itself.
#if defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include "arch/esp32/arch_audio_i2s.h"

#include <ESP_I2S.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 256 frames, matching the RP2 transport's BUFFER_SAMPLES. That number was
// re-derived there the hard way: shortening it to 64 to cut per-call ISR
// cost was measured as a NET LOSS, because more calls pay the same fixed
// overhead more often (see arch/rp2040/arch_audio_i2s.cpp). At 22050Hz a
// 256-frame block is 11.6ms of audio.
#define I2S_FRAMES 256

// One int32 IS one stereo frame. The HAL's fill contract packs each entry
// as (sample << 16) | (uint16_t)sample -- the same mono value in both
// halves -- so on a little-endian chip the four bytes are already
// [lo, hi, lo, hi], which is exactly left-then-right 16-bit stereo on the
// wire. The buffer goes to I2S verbatim: no conversion, no copy.
static int32_t s_buf[I2S_FRAMES];

static I2SClass s_i2s;
static volatile hal_audio_fill_cb g_fill_cb = NULL;
static bool s_running = false;

// CROSS-CORE CRITICAL SECTION, and a spinlock is the right primitive here
// specifically because the guarded regions are tiny.
//
// The obvious worry with portENTER_CRITICAL is holding it for a whole
// buffer mix -- hundreds of microseconds with interrupts off. That is not
// what happens: the machines' fill callbacks take this lock ONLY to
// snapshot their sound registers (a few dozen bytes) and then release it
// and mix from the copy. See pacman_audio_fill(). So this file must NOT
// wrap the callback in the lock; the callback owns its own use of it.
//
// A mutex would also work and would be wrong: it can block, and the game
// core would then sleep waiting on an audio task on the other core for
// what is a 30-byte copy.
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

uint32_t arch_i2s_enter_critical(void) {
    portENTER_CRITICAL(&s_mux);
    return 0;   // the lock itself carries the state; nothing to hand back
}

void arch_i2s_exit_critical(uint32_t saved_state) {
    (void)saved_state;
    portEXIT_CRITICAL(&s_mux);
}

void arch_i2s_set_fill_callback(hal_audio_fill_cb cb) {
    g_fill_cb = cb;
}

// Pinned to core 0. The Arduino loop -- and therefore the emulator and the
// whole video path -- runs on core 1, so audio and game do not compete, and
// the SPI driver's completion interrupts (registered on core 1, where the
// bus was set up) are unaffected by anything this task does with its own
// core's interrupt state.
static void i2s_task(void *arg) {
    (void)arg;
    for (;;) {
        hal_audio_fill_cb cb = g_fill_cb;
        if (cb) {
            cb(s_buf, I2S_FRAMES);
        } else {
            // Silence until a machine registers itself. Writing it rather
            // than idling keeps the I2S clocks running, which matters: the
            // MAX98357A mutes on loss of clock and pops on its return.
            for (int i = 0; i < I2S_FRAMES; i++) s_buf[i] = 0;
        }
        // Blocking, and that is the pacing. write() returns when the DMA
        // ring has room, so this task naturally runs at the sample rate
        // without a timer or a delay.
        s_i2s.write((uint8_t *)s_buf, sizeof s_buf);
    }
}

bool arch_i2s_init(uint32_t sample_rate, int bclk, int lrc, int din) {
    if (s_running) return true;

    s_i2s.setPins((int8_t)bclk, (int8_t)lrc, (int8_t)din);
    if (!s_i2s.begin(I2S_MODE_STD, (uint32_t)sample_rate,
                     I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
        return false;
    }

    // Priority above the Arduino loop task (which runs at 1) so audio is
    // not starved by a long frame, but below the drivers. 4KB of stack is
    // ample -- the mixers work from static state, not from the stack.
    BaseType_t ok = xTaskCreatePinnedToCore(i2s_task, "arcade_i2s", 4096,
                                            NULL, 2, NULL, 0);
    if (ok != pdPASS) return false;

    s_running = true;
    return true;
}

#endif // ARDUINO_ARCH_ESP32
