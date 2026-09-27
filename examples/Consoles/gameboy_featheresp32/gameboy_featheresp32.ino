// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// gameboy_featheresp32 -- a Game Boy (DMG) on the Adafruit Feather ESP32 V2
// with the 2.4" TFT FeatherWing, with the SD card as the cartridge: put ONE
// .gb ROM in /cart/ on the card. This sketch is the SAMP composition root,
// the one place that knows both the machine (src/machines/gb) and the board
// (src/boards/feather_esp32). extras/CONSOLES_PLAN.md, Phase 4.
//
// HOW IT RUNS (nes_featheresp32's model, from the Feather arcade sketches):
// the panel paints at most ~30 frames a second (320x240 over 40 MHz SPI), so
// each paint shows the second of TWO emulated frames, and the first is not
// drawn at all (DEVNOTES #140). Emulation runs on core 0; painting on core
// 1 (the Arduino loop), which hands core 0 its next two frames and then
// paints concurrently. A wall-clock limiter holds 60 Hz -- the rate the
// Fruit Jam runs the Game Boy at (locked to its display), which the audio
// rate is chosen for (GAMEBOY_APU_RATE in gameboy_core.h), so both boards
// play at the same speed and pitch.
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
//   Palette      --                   Y   (DMG green, Greys, Pocket, GBC)
//   Volume       --                   hold X, press Up / Down (3 dB steps)
//   L does nothing on the Game Boy, as Button 1 does nothing on the Fruit
//   Jam's.
//
// Battery saves: a battery cartridge's save RAM is kept in a standard .sav
// next to the ROM, the same file as the Fruit Jam's and PC emulators'. A
// save starts once the game's save RAM has been quiet for a second, and is
// written a sector per painted frame between frames, on the SPI bus the
// card shares with the display (DEVNOTES #144).
#include <Adafruit_Arcade_Machines.h>
#include <hal/arcade_hal_video.h>
#include <hal/arcade_hal_input.h>
#include <machines/gb/gameboy_machine.h>
#include <machines/gb/gameboy_audio.h>
#include <machines/gb/gameboy_palette.h>
#include <console/console_audio.h>
#include <console/console_save.h>
#include <boards/feather_esp32/hal_storage_feather_esp32.h>
#include <boards/feather_esp32/board_config_feather_esp32.h>
#include <boards/feather_esp32/wii_input_feather_esp32.h>
#include <input/wii_classic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define EMULATED_FRAMES_PER_PAINT 2u
// 60 Hz, as on the Fruit Jam (see the top of this file): 16,667 us a frame.
#define FRAME_US 16667u
#define FRAME_BUDGET_US (FRAME_US * EMULATED_FRAMES_PER_PAINT)
// Two frames of samples arrive at once, then nothing for a whole paint:
// the ring must sit deep enough for the consumer to ride that out
// (console_audio_set_target(), DEVNOTES #119).
#define AUDIO_RING_TARGET 1250u
// The MAX98357A amp has no volume control, and the Game Boy's full-scale
// output through it is very loud: Tetris averages -14 to -16 dBFS, some
// 9-11 dB above Super Mario Bros. 16 of 256 (-24 dB) was chosen by ear on
// the hardware, stepping down from 128 (DEVNOTES #142). The controller's
// X + Up/Down moves it from there.
#ifndef AUDIO_VOLUME
#define AUDIO_VOLUME 16u
#endif

static gameboy_system g_system;
static bool           g_cart_ok = false;
static uint16_t       g_error_color = 0;

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
        gameboy_emulate_frame(&g_system, (n % EMULATED_FRAMES_PER_PAINT) == 0);
        xTaskNotifyGive(g_video_task);
    }
}

static void print_cart(void) {
    Serial.printf("[gameboy-esp32] cart /cart/%s (%u bytes, title \"%s\", type 0x%02X, "
                  "%u candidate file(s), mounted on attempt %u, ROM buffer %u KB)\n",
                  g_system.cart_name, (unsigned)g_system.cart_size, g_system.cart_title,
                  (unsigned)g_system.cart_type, (unsigned)g_system.cart_matches,
                  (unsigned)g_system.mount_attempts, (unsigned)(g_system.rom_buffer_size / 1024u));
}

void setup() {
    // A deep transmit buffer, so a status line queues rather than blocking
    // the paint loop: at 115200 baud a line that overflows the default one
    // stalled a paint by ~45 ms, long enough to run the audio dry (#143).
    Serial.setTxBufferSize(2048);
    Serial.begin(115200);
    delay(1500);
    Serial.println("[gameboy-esp32] boot");

    gameboy_init(&g_system);
    g_cart_ok = gameboy_load_cart(&g_system, &g_error_color);
    if (g_cart_ok) print_cart();
    else Serial.printf("[gameboy-esp32] cart FAILED: %s\n",
                       gameboy_boot_error_text(g_system.boot_error));

    // The controller, if one is plugged in now or later. After the cart
    // load, which starts the input task it runs on.
    feather_wii_input_begin(FEATHER_WII_MAP_CONSOLE);

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
        // an underrun. Start the counters here, with the game (#141).
        { gameboy_audio_stats_t discard; gameboy_audio_take_stats(&discard); }
        g_video_task = xTaskGetCurrentTaskHandle();
        xTaskCreatePinnedToCore(emulation_task, "gb_emu", 8192, NULL, 2, &g_emu_task, 0);
        for (uint32_t f = 0; f < EMULATED_FRAMES_PER_PAINT; f++) xTaskNotifyGive(g_emu_task);
    }
    Serial.printf("[gameboy-esp32] heap free %u, largest block %u, internal free %u, PSRAM free %u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)ESP.getFreePsram());
}

void loop() {
    if (!g_cart_ok) {
        static uint32_t last = 0;
        if (millis() - last > 2000) {
            last = millis();
            Serial.printf("[gameboy-esp32] boot error: %s\n",
                          gameboy_boot_error_text(g_system.boot_error));
        }
        gameboy_draw_error_frame(g_error_color);
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
    bool palette_next = hal_input_read(HAL_BTN_MIRROR); // the controller's Y

    // The controller's X + Up/Down. Counted on the input task, so a tap
    // between two paints still counts.
    if (const int steps = feather_wii_input_take_volume_steps())
        Serial.printf("[gameboy-esp32] volume %lu\n",
                      (unsigned long)console_audio_volume_step(steps));

#ifdef TEST_AUTOSTART
    // gameboy_fruitjam's Tetris script, counted in emulated frames: Start on
    // the title, game-type and music screens, then drift left and right.
    {
        static uint32_t paint = 0;
        const uint32_t f = (++paint) * EMULATED_FRAMES_PER_PAINT;
        if ((f > 900u && f < 910u) || (f > 1000u && f < 1010u) ||
            (f > 1100u && f < 1110u) || (f > 1200u && f < 1210u)) start = true;
        if (f > 1300u) { left = ((f / 40u) & 1u) != 0; right = !left; }
    }
#endif

    static uint32_t paint = 0, t_prev = 0, paint_sum = 0, paint_max = 0;
    const uint32_t t0 = micros();

    // 1. Wait until core 0 has finished both frames: the machine is idle.
    for (uint32_t f = 0; f < EMULATED_FRAMES_PER_PAINT; f++) ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    // 2. In that idle window: new input for the next frames, and the drawn
    //    frame becomes the one to paint.
    gameboy_input_update(&g_system, up, down, left, right, a, b, start, select,
                         rotate, palette_next);
    gameboy_present(&g_system);
    static uint8_t palette_shown = 0xFF;
    if (g_system.palette != palette_shown) {
        palette_shown = g_system.palette;
        Serial.printf("[gameboy-esp32] palette %s\n",
                      gameboy_palette_name((gameboy_palette_t)palette_shown));
    }
    // 3. Release core 0 for the next two frames, and paint concurrently.
    for (uint32_t f = 0; f < EMULATED_FRAMES_PER_PAINT; f++) xTaskNotifyGive(g_emu_task);
    gameboy_paint(&g_system);
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
            gameboy_take_emulate_us(&discard_mean, &discard_max);
            return;
        }
        const float fps = 30000.0f / (float)(now - t_prev);
        t_prev = now;
        uint32_t emu_mean, emu_max;
        gameboy_take_emulate_us(&emu_mean, &emu_max);
        gameboy_audio_stats_t as;
        gameboy_audio_take_stats(&as);
        feather_wii_input_stats_t ws;
        feather_wii_input_get_stats(&ws);
        Serial.printf("[gameboy-esp32] paint %lu: %.1f fps display, %.1f fps emulated (%.0f%% of 60 Hz); "
                      "paint mean %lu max %lu us; emulate mean %lu max %lu us (budget %u); "
                      "audio ur %lu ov %lu min %lu depth %lu gen_max %lu us; rot %u, pad 0x%02X, "
                      "core_err %lu; wii %s%s 0x%04X (drops %lu, skipped %lu), volume %lu\n",
                      (unsigned long)paint, fps, fps * EMULATED_FRAMES_PER_PAINT,
                      100.0f * fps * EMULATED_FRAMES_PER_PAINT / 60.0f,
                      (unsigned long)(paint_sum / 30u), (unsigned long)paint_max,
                      (unsigned long)emu_mean, (unsigned long)emu_max, FRAME_US,
                      (unsigned long)as.underruns, (unsigned long)as.overruns,
                      (unsigned long)as.min_depth, (unsigned long)as.depth,
                      (unsigned long)as.gen_us_max,
                      (unsigned)g_system.rotation, (unsigned)g_system.pad,
                      (unsigned long)gameboy_core_errors(),
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
                Serial.printf("[gameboy-esp32] save %s %s: loaded %s, saves %lu, last %lu frames, "
                              "busy %lu, errors %lu, bus step max %lu us\n",
                              kState[ss.state], ss.path, ss.loaded ? "yes" : "no",
                              (unsigned long)ss.saves, (unsigned long)ss.last_save_frames,
                              (unsigned long)ss.busy_waits, (unsigned long)ss.errors,
                              (unsigned long)feather_storage_take_service_us_max());
            }
        }
        // Every tenth heartbeat, which cartridge this is (the boot line is
        // lost if the serial port opened late).
        if ((paint % 300u) == 60u) print_cart();
    }
}
