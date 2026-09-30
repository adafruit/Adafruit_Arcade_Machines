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
// Controls: the Feather's own buttons, and a Wii Classic or SNES Classic
// controller on STEMMA QT through the Wii Nunchuck breakout, either or both
// at once (wii_input_feather_esp32.h).
//
//                Feather button       Controller
//   D-pad        UP 7, DOWN 8,        D-pad
//                LEFT 39, RIGHT 36
//   A            SHOOT (4)            A
//   B            START2 (34)          B
//   Start        START1 (25)          Start (+ on the Wii Classic)
//   Select       COIN (26)            Select (-)
//   Rotation     ROTATE (37)          R
//   8:7 aspect   --                   L
//   Palette      --                   Y
//   Volume       --                   hold X, press Up / Down (3 dB steps,
//                                        the lowest one mute)
//
// Settings: the rotation, palette, 8:7 stretch and volume last chosen are
// kept per game in /cart/<rom name>.feather.cfg, a small text file, and
// used again at the next power-up (settings/settings.h, DEVNOTES #156).
//
// Battery saves: a battery cartridge's save RAM is kept in a standard .sav
// next to the ROM, the same file as the Fruit Jam's and PC emulators'. A
// save starts once the game's save RAM has been quiet for a second, and is
// written a sector per painted frame between frames, on the SPI bus the
// card shares with the display (DEVNOTES #144).
#include <Adafruit_Arcade_Machines.h>
#include <hal/arcade_hal_video.h>
#include <hal/arcade_hal_input.h>
#include <machines/nes/nes_machine.h>
#include <machines/nes/nes_core.h>
#include <console/console_audio.h>
#include <console/console_save.h>
#include <settings/settings.h>
#include <boards/feather_esp32/hal_storage_feather_esp32.h>
#include <boards/feather_esp32/board_config_feather_esp32.h>
#include <boards/feather_esp32/wii_input_feather_esp32.h>

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
// ear on the hardware (DEVNOTES #142). The controller's X + Up/Down moves
// it from there.
#ifndef AUDIO_VOLUME
#define AUDIO_VOLUME 48u
#endif

static nes_system g_system;
static bool       g_cart_ok = false;
static uint16_t   g_error_color = 0;

// SETTINGS SAVED TO THE CARD (settings/settings.h): this game's rotation,
// palette, 8:7 stretch and volume on this board, in
// /cart/<rom name>.feather.cfg, read at boot and rewritten 3 s after a
// change. The defaults are this sketch's own.
static const char *const kOnOff[] = { "off", "on" };
static int g_set_rotation, g_set_palette, g_set_stretch, g_set_volume;
static uint32_t g_settings_saves_shown = 0;

static void print_settings(const char *what) {
    char values[128];
    settings_describe(values, sizeof values);
    settings_stats_t st;
    settings_take_stats(&st);
    static const char *const kState[] = { "none", "UNAVAILABLE", "ready", "writing" };
    Serial.printf("[nes-esp32] settings %s %s (%s): %s; read %lu, ignored %lu%s, saves %lu, errors %lu\n",
                  what, st.path, kState[st.state], values, (unsigned long)st.applied,
                  (unsigned long)st.ignored, st.truncated ? ", TRUNCATED" : "",
                  (unsigned long)st.saves, (unsigned long)st.errors);
}

// Boot only, with the card mounted (after the cart load): read them and
// apply them over the defaults.
static void load_settings(void) {
    g_set_rotation = settings_add_choice("rotation", SETTINGS_ROTATION_NAMES, 4,
                                         g_system.rotation, nullptr);
    g_set_palette  = settings_add_choice("palette", nes_core_palette_keys(),
                                         (uint8_t)nes_core_palette_count(), g_system.palette, nullptr);
    g_set_stretch  = settings_add_choice("stretch", kOnOff, 2, g_system.stretch ? 1 : 0,
                                         "off, on (8:7 aspect)");
    g_set_volume   = settings_add_int("volume", 0, 256, AUDIO_VOLUME, "0 (mute) to 256");
    char path[96];
    settings_console_path(g_system.cart_name, "feather", path, sizeof path);
    settings_begin(path, "NES settings for this game on the Feather ESP32 V2.");
    g_system.rotation = (uint8_t)settings_get(g_set_rotation);
    g_system.palette  = (uint8_t)settings_get(g_set_palette);
    g_system.stretch  = settings_get(g_set_stretch) != 0;
    print_settings("loaded");
}

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
    // A deep transmit buffer, so a status line queues rather than blocking
    // the paint loop: at 115200 baud a line that overflows the default one
    // stalled a paint by ~45 ms, long enough to run the audio dry (#143).
    Serial.setTxBufferSize(2048);
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
    else
        load_settings();

    // Hand the SPI bus to the IDF display driver -- AFTER the cartridge is
    // read over SPIClass; the two cannot both own it (arch_spi_dma.h).
    // The controller, if one is plugged in now or later. After the cart
    // load, which starts the input task it runs on.
    feather_wii_input_begin(FEATHER_WII_MAP_CONSOLE);

    hal_video_run();

    // Only when the cart loaded: an emulation task started without one runs
    // a machine that was never set up, and on this board that is a reboot
    // loop that hides the error screen (DEVNOTES #123).
    if (g_cart_ok) {
        console_audio_set_target(AUDIO_RING_TARGET);
        console_audio_set_volume((uint32_t)settings_get(g_set_volume));
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
    bool b      = hal_input_read(HAL_BTN_START2) || hal_input_read(HAL_BTN_ACTION2);
    bool start  = hal_input_read(HAL_BTN_START1);
    bool select = hal_input_read(HAL_BTN_COIN);
    bool rotate = hal_input_read(HAL_BTN_ROTATE);
    bool palette_next = hal_input_read(HAL_BTN_MIRROR);  // the controller's Y
    bool stretch      = hal_input_read(HAL_BTN_STRETCH); // the controller's L

    // The controller's X + Up/Down. Counted on the input task, so a tap
    // between two paints still counts.
    if (const int steps = feather_wii_input_take_volume_steps())
        Serial.printf("[nes-esp32] volume %lu\n",
                      (unsigned long)console_audio_volume_step(steps));

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
    nes_input_update(&g_system, up, down, left, right, a, b, start, select,
                     rotate, palette_next, stretch);
    nes_present(&g_system);
    static uint8_t palette_shown = 0xFF;
    static bool stretch_shown = false;
    if (g_system.palette != palette_shown || g_system.stretch != stretch_shown) {
        palette_shown = g_system.palette;
        stretch_shown = g_system.stretch;
        Serial.printf("[nes-esp32] palette %s, stretch %s\n",
                      nes_core_palette_name(palette_shown), stretch_shown ? "on" : "off");
    }
    // Settings, also in the idle window, where core 0 isn't changing any of
    // these: a change is saved 3 s after the last one, one storage step a
    // paint.
    settings_set(g_set_rotation, g_system.rotation);
    settings_set(g_set_palette, g_system.palette);
    settings_set(g_set_stretch, g_system.stretch ? 1 : 0);
    settings_set(g_set_volume, (int32_t)console_audio_volume());
    settings_frame();
    {
        settings_stats_t st;
        settings_take_stats(&st);
        if (st.saves != g_settings_saves_shown) {
            g_settings_saves_shown = st.saves;
            print_settings("saved");
        }
    }
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
        feather_wii_input_stats_t ws;
        feather_wii_input_get_stats(&ws);
        Serial.printf("[nes-esp32] paint %lu: %.1f fps display, %.1f fps emulated (%.0f%% of 60.1 Hz); "
                      "paint mean %lu max %lu us; emulate mean %lu max %lu us (budget 16639); "
                      "audio ur %lu ov %lu min %lu depth %lu; rot %u, pad 0x%02X; "
                      "wii %s%s 0x%04X (drops %lu, skipped %lu), volume %lu\n",
                      (unsigned long)paint, fps, fps * EMULATED_FRAMES_PER_PAINT,
                      100.0f * fps * EMULATED_FRAMES_PER_PAINT / 60.0988f,
                      (unsigned long)(paint_sum / 30u), (unsigned long)paint_max,
                      (unsigned long)emu_mean, (unsigned long)emu_max,
                      (unsigned long)as.underruns, (unsigned long)as.overruns,
                      (unsigned long)as.min_depth, (unsigned long)as.depth,
                      (unsigned)g_system.rotation, (unsigned)g_system.pad,
                      ws.connected ? "connected" : "absent", ws.hires ? " hires" : "",
                      (unsigned)ws.buttons, (unsigned long)ws.drops,
                      (unsigned long)(ws.request_fails + ws.read_fails),
                      (unsigned long)console_audio_volume());
        paint_sum = paint_max = 0;
        {
            // Battery saves, only for a cartridge that has them. Short on
            // purpose (see Serial.setTxBufferSize above).
            console_save_stats_t ss;
            console_save_take_stats(&ss);
            if (ss.state != CONSOLE_SAVE_NONE) {
                static const char *const kState[] = { "none", "UNAVAILABLE", "ready", "writing" };
                Serial.printf("[nes-esp32] save %s %s: loaded %s, saves %lu, last %lu frames "
                              "%lu sectors (%lu since boot), busy %lu, errors %lu, bus step max %lu us\n",
                              kState[ss.state], ss.path, ss.loaded ? "yes" : "no",
                              (unsigned long)ss.saves, (unsigned long)ss.last_save_frames,
                              (unsigned long)ss.last_save_sectors, (unsigned long)ss.sectors_written,
                              (unsigned long)ss.busy_waits, (unsigned long)ss.errors,
                              (unsigned long)feather_storage_take_service_us_max());
            }
        }
    }
}
