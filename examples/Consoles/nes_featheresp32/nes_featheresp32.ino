// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// nes_featheresp32 -- the NES on the Adafruit Feather ESP32 V2 with the 2.4"
// TFT FeatherWing, with the SD card as the cartridge: put ONE .nes (iNES)
// ROM in /cart/ on the card. This sketch is the SAMP composition root, the
// one place that knows both the machine (src/machines/nes) and the board
// (src/boards/feather_esp32). extras/CONSOLES_PLAN.md, Phase 4.
//
// The emulator core is nofrendo, vendored from retro-go under the GPL
// version 2 (src/machines/nes/core/VENDORED.md): a firmware built from this
// sketch includes GPL code.
//
// HOW IT RUNS (the Feather arcade sketches' model, e.g. galaga_featheresp32):
// the panel paints at most ~30 frames a second (320x240 over 40 MHz SPI), so
// each paint shows the second of TWO emulated frames, and the first is not
// drawn at all (DEVNOTES #140). Emulation runs on core 0; painting on core
// 1 (the Arduino loop), which hands core 0 its next two frames and then
// paints concurrently. A wall-clock limiter holds the NES's own 60.0988 Hz.
//
// Controls, for now the Feather's own buttons (a Wii Classic controller
// over I2C comes next, extras/CONSOLES_PLAN.md):
//   D-pad   UP 7, DOWN 8, LEFT 39, RIGHT 36
//   A       SHOOT (4)        B       START2 (34)
//   Start   START1 (25)      Select  COIN (26)
//   ROTATE (37) cycles the picture's rotation.
//
// Battery saves are not available on this board yet: its SD card shares
// the SPI bus with the display (CONSOLES_PLAN.md, Phase 4); a battery
// cartridge plays, and its save RAM is not kept.
#include <Adafruit_Arcade_Machines.h>
#include <hal/arcade_hal_video.h>
#include <hal/arcade_hal_input.h>
#include <machines/nes/nes_machine.h>
#include <machines/nes/nes_core.h>
#include <console/console_audio.h>
#include <boards/feather_esp32/board_config_feather_esp32.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define EMULATED_FRAMES_PER_PAINT 2u
// The NES (NTSC) refreshes at 60.0988 Hz: 16,639 us a frame.
#define FRAME_BUDGET_US (16639u * EMULATED_FRAMES_PER_PAINT)
// Two frames of samples arrive at once, then nothing for a whole paint:
// the ring must sit deep enough for the consumer to ride that out
// (console_audio_set_target(), DEVNOTES #119).
#define AUDIO_RING_TARGET 1250u
// The MAX98357A amp has no volume control of its own (console_audio_set_volume();
// 256 = full). 48 (-14.5 dB) matches gameboy_featheresp32's 16 by the
// source levels (SMB is ~10 dB quieter than Tetris) and was confirmed by
// ear on the hardware (DEVNOTES #142).
#ifndef AUDIO_VOLUME
#define AUDIO_VOLUME 48u
#endif

static nes_system g_system;
static bool       g_cart_ok = false;
static uint16_t   g_error_color = 0;

static TaskHandle_t g_emu_task = NULL;
static TaskHandle_t g_video_task = NULL;

// One notification per emulated frame, acked when done: the handshake the
// Feather arcade sketches use, which bounds the outstanding work and gives
// core 1 a window in which the machine is quiescent (galaga_featheresp32).
// Only the last frame of each paint is drawn.
static void emulation_task(void *arg) {
    (void)arg;
    uint32_t n = 0;
    for (;;) {
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        n++;
        nes_emulate_frame(&g_system, (n % EMULATED_FRAMES_PER_PAINT) == 0);
        xTaskNotifyGive(g_video_task);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("[nes-esp32] boot");

    nes_init_system(&g_system);
    g_cart_ok = nes_load_cart(&g_system, &g_error_color);
    Serial.printf("[nes-esp32] cart %s: %s (%u bytes, mapper %d %s)\n",
                  g_cart_ok ? "loaded" : "FAILED", g_system.cart_name,
                  (unsigned)g_system.cart_size, g_system.mapper, g_system.mapper_name);
    if (!g_cart_ok)
        Serial.printf("[nes-esp32]   %s\n", nes_boot_error_text(g_system.boot_error));

    // Hand the SPI bus to the IDF display driver -- AFTER the cartridge is
    // read over SPIClass; the two cannot both own it (arch_spi_dma.h).
    hal_video_run();

    // Only when the cart loaded: an emulation task started without one runs
    // a machine that was never set up, and on this board that is a reboot
    // loop that hides the error screen (DEVNOTES #123).
    if (g_cart_ok) {
        console_audio_set_target(AUDIO_RING_TARGET);
        console_audio_set_volume(AUDIO_VOLUME);
        // The audio pump has been playing silence since the cartridge load
        // (through the display init), and each of those samples counted as
        // an underrun. Start the counters here, with the game.
        { console_audio_stats_t discard; console_audio_take_stats(&discard); }
        g_video_task = xTaskGetCurrentTaskHandle();
        xTaskCreatePinnedToCore(emulation_task, "nes_emu", 8192, NULL, 2, &g_emu_task, 0);
        for (uint32_t f = 0; f < EMULATED_FRAMES_PER_PAINT; f++) xTaskNotifyGive(g_emu_task);
    }
    Serial.printf("[nes-esp32] heap free %u, largest block %u, internal free %u, PSRAM free %u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)ESP.getFreePsram());
}

void loop() {
    if (!g_cart_ok) {
        static uint32_t last = 0;
        if (millis() - last > 2000) {
            last = millis();
            Serial.printf("[nes-esp32] boot error: %s\n", nes_boot_error_text(g_system.boot_error));
        }
        nes_draw_error_frame(g_error_color);
        return;
    }

    bool up     = hal_input_read(HAL_BTN_UP);
    bool down   = hal_input_read(HAL_BTN_DOWN);
    bool left   = hal_input_read(HAL_BTN_LEFT);
    bool right  = hal_input_read(HAL_BTN_RIGHT);
    bool a      = hal_input_read(HAL_BTN_SHOOT);
    bool b      = hal_input_read(HAL_BTN_START2);
    bool start  = hal_input_read(HAL_BTN_START1);
    bool select = hal_input_read(HAL_BTN_COIN);
    bool rotate = hal_input_read(HAL_BTN_ROTATE);

#ifdef TEST_AUTOSTART
    // The host harnesses' script, counted in emulated frames.
    {
        static uint32_t paint = 0;
        const uint32_t f = (++paint) * EMULATED_FRAMES_PER_PAINT;
        if ((f >= 200 && f <= 205) || (f >= 400 && f <= 405) || (f >= 600 && f <= 605)) start = true;
        if (f >= 700 && f <= 1500) right = true;
        if ((f >= 800 && f <= 815) || (f >= 900 && f <= 915) || (f >= 1000 && f <= 1015) ||
            (f >= 1100 && f <= 1115)) a = true;
    }
#endif

    static uint32_t paint = 0, t_prev = 0, paint_sum = 0, paint_max = 0;
    const uint32_t t0 = micros();

    // 1. Wait until core 0 has finished both frames: the machine is idle.
    for (uint32_t f = 0; f < EMULATED_FRAMES_PER_PAINT; f++) ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    // 2. In that idle window: new input for the next frames, and the drawn
    //    frame becomes the one to paint.
    nes_input_update(&g_system, up, down, left, right, a, b, start, select, rotate, false, false);
    nes_present(&g_system);
    // 3. Release core 0 for the next two frames, and paint concurrently.
    for (uint32_t f = 0; f < EMULATED_FRAMES_PER_PAINT; f++) xTaskNotifyGive(g_emu_task);
    nes_paint(&g_system);
    const uint32_t painted = micros() - t0;
    paint_sum += painted;
    if (painted > paint_max) paint_max = painted;

    // WALL-CLOCK LIMITER, repaying short overruns and resyncing only past a
    // whole budget (galaga_featheresp32, DEVNOTES #122).
    static uint32_t deadline = 0;
    if (deadline == 0) deadline = micros();
    deadline += FRAME_BUDGET_US;
    const int32_t slack = (int32_t)(deadline - micros());
    if (slack > 0)                              delayMicroseconds((uint32_t)slack);
    else if (slack < -(int32_t)FRAME_BUDGET_US) deadline = micros();

    if (++paint % 30u == 0) {
        const uint32_t now = millis();
        if (t_prev == 0) {
            // The first window spans boot; prime the clock and skip it.
            uint32_t discard_mean, discard_max;
            t_prev = now; paint_sum = paint_max = 0;
            nes_take_emulate_us(&discard_mean, &discard_max);
            return;
        }
        const float fps = 30000.0f / (float)(now - t_prev);
        t_prev = now;
        uint32_t emu_mean, emu_max;
        nes_take_emulate_us(&emu_mean, &emu_max);
        console_audio_stats_t as;
        console_audio_take_stats(&as);
        Serial.printf("[nes-esp32] paint %lu: %.1f fps display, %.1f fps emulated (%.0f%% of 60.1 Hz); "
                      "paint mean %lu max %lu us; emulate mean %lu max %lu us (budget 16639); "
                      "audio ur %lu ov %lu min %lu depth %lu; rot %u, pad 0x%02X\n",
                      (unsigned long)paint, fps, fps * EMULATED_FRAMES_PER_PAINT,
                      100.0f * fps * EMULATED_FRAMES_PER_PAINT / 60.0988f,
                      (unsigned long)(paint_sum / 30u), (unsigned long)paint_max,
                      (unsigned long)emu_mean, (unsigned long)emu_max,
                      (unsigned long)as.underruns, (unsigned long)as.overruns,
                      (unsigned long)as.min_depth, (unsigned long)as.depth,
                      (unsigned)g_system.rotation, (unsigned)g_system.pad);
        paint_sum = paint_max = 0;
    }
}
