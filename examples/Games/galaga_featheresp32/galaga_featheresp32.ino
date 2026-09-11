// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// galaga_featheresp32 -- Galaga on an Adafruit Feather ESP32 V2 with the
// 2.4" TFT FeatherWing. The SAMP composition root for this pair: the only
// place that knows both "this game" and "this board".
//
// ~32fps, not 60, and the reason is arithmetic rather than anything
// fixable in this sketch. The wing's SPI pins are not the ESP32's IOMUX
// pins, so the GPIO matrix caps the bus at 40MHz; a 320x240 RGB565 frame is
// 153,600 bytes, which is 30.7ms of clocking that has to happen no matter
// what the CPU is doing. Measured frame time is 31.4ms -- within 2% of that
// ceiling, because the scanlines go out by DMA and the Z80 emulation now
// runs underneath the transfer instead of after it (see
// src/arch/esp32/arch_spi_dma.h). Emulation is 15.1ms of that 31.4 and is
// essentially free now; the only lever left is the clock.
//
// For scale: the same code on the FIFO path the Arduino core provides ran
// 59.7ms/frame at 16.8fps, because a 64-byte poll loop added 14ms of pure
// overhead AND could not overlap with anything.
//
// Audio is a Stereo I2S 3W amp, dual MAX98357A (Adafruit #6513): BCLK to
// IO27, LRC to IO12, DIN to IO13, Vin to VBUS. It runs on its own FreeRTOS
// task pinned to core 0, so it does not compete with the emulator and the
// video path on core 1.
//
// Differences from pacman_fruitjam.ino, all forced by the hardware:
//  - no setup1()/loop1(): there is no second-core display pump here, the
//    panel is written from submit_scanline() on this core.
//  - no set_sys_clock_khz(): that is a Pico SDK call. The ESP32 runs at
//    240MHz from the board config.
//  - HAL_BTN_MIRROR and HAL_BTN_STRETCH are not wired and read false.
#include <Adafruit_Arcade_Machines.h>
#include <hal/arcade_hal_video.h>
#include <hal/arcade_hal_input.h>
#include <boards/feather_esp32/board_config_feather_esp32.h>
#include <machines/galaga/galaga_machine.h>
#include <machines/galaga/galaga_video.h>
#include <machines/galaga/galaga_input.h>

// Two emulated frames per painted frame -- see pacman_run_frames(). The
// panel cannot reach 60Hz, so the game would otherwise run in slow motion.
#define EMULATED_FRAMES_PER_PAINT 2u

// Pac-Man's real refresh is 60.606Hz, so one emulated frame is 16,500us.
// The budget covers however many frames are emulated per paint.
#define FRAME_BUDGET_US (16500u * EMULATED_FRAMES_PER_PAINT)

// Set to 1 to time the transport in isolation at boot. See setup().
#define GALAGA_ESP32_BENCH 0

static galaga_system g_system;

// --- TWO-CORE PROTOTYPE ---------------------------------------------------
//
// Emulation on core 0, video on core 1, which is what galagino does and the
// one technique of its four we were missing. We already batch scanlines
// into strips, pace to a wall clock, and trigger vblank twice per paint --
// but emulation and video were welded to core 1 by the interleaved frame
// loop, leaving core 0 running only the small audio and input tasks.
//
// Why it matters HERE and not for the other two games: Pac-Man and
// Ms. Pac-Man hold 100% of arcade speed already, so the second core would
// only buy headroom. Galaga does not -- it costs ~22ms per frame under
// sprite load, and serialised with ~31.7ms of render-and-transport that is
// ~44ms a paint, about 76% speed. Run concurrently it should be
// max(22, 31.7), i.e. transport-bound and 100%.
//
// The sync is a notification per emulated frame: video signals, emulation
// runs one frame. Same shape as galagino's xTaskNotifyGive.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static TaskHandle_t g_emu_task = NULL;

static TaskHandle_t g_video_task = NULL;

// HANDSHAKE, NOT A ONE-WAY STREAM, and the first version got this wrong in
// a way worth recording. It sent a notification per emulated frame and
// never waited for a reply. Under sprite load core 0 needs ~44ms to do the
// work core 1 asks for every ~33ms, so the notification count ACCUMULATED,
// ulTaskNotifyTake stopped blocking, this task ran forever, and core 0's
// idle task starved until the task watchdog aborted -- "crashes depending
// on what the attract loop is doing", which is exactly what an
// accumulating queue looks like.
//
// Acking bounds the outstanding count at EMULATED_FRAMES_PER_PAINT, so
// this task always returns to a blocking wait and idle always runs. It
// also gives core 1 a window in which the machine is provably quiescent,
// which is where the sprite latch has to happen.
static void emulation_task(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        galaga_run_cpu_frames(&g_system, 1);
        xTaskNotifyGive(g_video_task);   // "frame done"
    }
}
static bool     g_assets_ok = false;
static uint16_t g_error_color = 0;

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("[galaga-esp32] boot: serial up");

    galaga_init(&g_system);
    Serial.printf("[galaga-esp32] boot: pacman_init done, rotation %u\n",
                  (unsigned)g_system.rotation);

    g_assets_ok = galaga_load_assets(&g_system, &g_error_color);
    Serial.printf("[galaga-esp32] boot: assets %s\n",
                  g_assets_ok ? "loaded OK" : "FAILED");
    if (!g_assets_ok) {
        Serial.printf("[galaga-esp32]   error colour 0x%04X -- red means no SD "
                      "card / would not mount, yellow means mounted but the "
                      "required ROM files were missing\n", g_error_color);
    }
    // Hand the SPI bus to the IDF driver. Must come AFTER asset loading --
    // SdFat reads the ROMs over SPIClass, and the two cannot both own the
    // peripheral. See arch_spi_dma.h.
    hal_video_run();

    // Emulation on core 0, priority above the Arduino loop (1) so a long
    // paint cannot delay it, below the audio and input tasks (2) which are
    // tiny and must not jitter. Started AFTER assets load -- it would
    // otherwise run a machine with no ROM in it.
    g_video_task = xTaskGetCurrentTaskHandle();
    xTaskCreatePinnedToCore(emulation_task, "galaga_emu", 8192, NULL, 2,
                            &g_emu_task, 0);
    // Prime the pipeline: loop() opens by waiting for acks, so core 0 needs
    // work to ack before the first paint.
    for (uint32_t f = 0; f < EMULATED_FRAMES_PER_PAINT; f++) {
        xTaskNotifyGive(g_emu_task);
    }

    // TRANSPORT BENCHMARK, one shot. Off by default: it costs ~0.7s of boot
    // and is a tool, not a feature. Turn it on when changing anything about
    // the transport -- it is what found that per-transfer overhead, not
    // pixel throughput, was the ceiling (DEVNOTES #111).
#if GALAGA_ESP32_BENCH
    // Original note: Pushes frames with NO rendering and no
    // emulation, so what is left is the transport alone: the byte swap plus
    // whatever the driver costs per transfer. Compared against the wire
    // floor -- 153,600 bytes at 40MHz is 30,720us -- this says how much of
    // the frame is overhead rather than physics.
    {
        const uint32_t t0 = micros();
        for (int f = 0; f < 20; f++) {
            for (uint32_t y = 0; y < HAL_VIDEO_HEIGHT; y++) {
                uint16_t *b = hal_video_acquire_scanline();
                hal_video_submit_scanline(b);
            }
        }
        const uint32_t per = (micros() - t0) / 20u;
        Serial.printf("[bench] transport only: %lu us/frame "
                      "(40MHz wire floor 30720us, overhead %ld us)\n",
                      (unsigned long)per, (long)per - 30720L);
    }
#endif

    Serial.printf("[galaga-esp32] heap free %u, largest block %u, PSRAM %u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                  (unsigned)ESP.getPsramSize());
}

void loop() {
    if (!g_assets_ok) {
        static uint32_t last = 0;
        if (millis() - last > 1000) {
            last = millis();
            Serial.println("[galaga-esp32] asset load failed -- halted");
        }
        galaga_draw_error_frame(g_error_color);
        return;
    }

    bool coin   = hal_input_read(HAL_BTN_COIN);
    bool start1 = hal_input_read(HAL_BTN_START1);
    bool start2 = hal_input_read(HAL_BTN_START2);
    bool fire   = hal_input_read(HAL_BTN_SHOOT);   // wired on this board
    bool left   = hal_input_read(HAL_BTN_LEFT);
    bool right  = hal_input_read(HAL_BTN_RIGHT);
    bool rotate = hal_input_read(HAL_BTN_ROTATE);
    bool mirror = hal_input_read(HAL_BTN_MIRROR);   // always false here

    // Galaga is a shooter: left/right/fire, no up/down. HAL_BTN_SHOOT is
    // wired on this board (GPIO 4) and was simply unused by Pac-Man.
    galaga_input_update(&g_system, coin, start1, start2,
                        left, right, fire, rotate, mirror);

    // Whole-frame time: emulation and pixel-pushing together, because they
    // are no longer separable. submit_scanline() hands the row to DMA and
    // returns, so the next scanline's Z80 cycles run while it is still on
    // the wire -- the same overlap the Fruit Jam gets from a second core,
    // bought here with a second buffer instead. A number close to 30.7ms
    // means the transfer is the whole cost and the emulator is hidden
    // inside it; a number well above that means something stopped fitting.
    static uint32_t frame = 0, emul_us = 0, push_us = 0, t_prev = 0;
    uint32_t t0 = micros();

    // TWO EMULATED FRAMES PER PAINTED FRAME.
    //
    // This board's panel cannot reach 60Hz -- 320x240 RGB565 at 40MHz is
    // 30.7ms of unavoidable clocking -- so the game would otherwise run in
    // slow motion, advancing one frame per display frame. Decoupling them
    // keeps the Z80 at the interrupt rate the real cabinet produced and
    // decimates only the picture, which is what galagino does on the same
    // class of hardware for the same reason.
    //
    // It is nearly free: most of a frame is already spent waiting for the
    // SPI transfer, and the second frame's cycles fit inside that wait.
    // Paint from whatever the emulation core has produced, then ask it for
    // the next EMULATED_FRAMES_PER_PAINT frames. The two overlap: the
    // notifications are sent BEFORE this core starts its next paint, so
    // core 0 emulates while core 1 renders and transmits.
    // 1. Wait until core 0 has finished everything asked of it. The machine
    //    is now quiescent -- nothing is mutating sprite registers.
    for (uint32_t f = 0; f < EMULATED_FRAMES_PER_PAINT; f++) {
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    }

    // 2. Latch this frame's sprites from that quiescent state. Short -- the
    //    only part that cannot overlap.
    galaga_video_begin_frame(&g_system);

    // 3. Release core 0 for the next frames. From here the two run
    //    concurrently: core 0 emulates while core 1 paints.
    for (uint32_t f = 0; f < EMULATED_FRAMES_PER_PAINT; f++) {
        xTaskNotifyGive(g_emu_task);
    }

    // 4. Paint. Reads video_ram live, which is safe: tiles change rarely
    //    and by whole character cells, and this renderer already accepts
    //    intra-frame staleness as authentic CRT behaviour.
    galaga_render_frame(&g_system);

    // WALL-CLOCK LIMITER. Without it the game runs at whatever rate the
    // panel happens to allow, which measured 61.4 emulated fps against a
    // real Pac-Man cabinet's 60.606Hz -- 1.6% fast, and it would drift with
    // scene complexity. Waiting out the remainder of the budget makes speed
    // exact and content-independent.
    //
    // It can only ever slow things down. If a frame overruns the budget the
    // deadline is simply reset, so a heavy scene degrades to "as fast as
    // possible" rather than accumulating a debt it can never repay.
    static uint32_t deadline = 0;
    if (deadline == 0) deadline = micros();
    deadline += FRAME_BUDGET_US;
    int32_t slack = (int32_t)(deadline - micros());
    if (slack > 0) delayMicroseconds((uint32_t)slack);
    else           deadline = micros();

    uint32_t total = micros() - t0;

    emul_us += total;
    if (++frame % 30u == 0) {
        uint32_t now = millis();
        // The first window spans boot, so its rate is meaningless -- it
        // printed "18% of 60.6Hz", which reads like a fault. Prime the
        // clock and skip it.
        if (t_prev == 0) { t_prev = now; emul_us = 0; push_us = 0; return; }
        float fps = 30000.0f / (float)(now - t_prev);
        t_prev = now;
        // Rotation is in the heartbeat because it is not otherwise
        // observable and it changes what the geometry code is doing: 1 and
        // 3 are tate (the picture fills all 320x240), 0 and 2 are yoko (180
        // columns pillarboxed inside 320). It also makes a stray ROTATE
        // press visible -- GPIO 37 is input-only with no internal pull, so
        // an unwired or floating button line cycles this silently.
        Serial.printf("[galaga-esp32] frame %lu  %.1f fps display  "
                      "%.1f fps emulated (%.0f%% of 60.6Hz)  frame %lu us  "
                      "rot %u\n",
                      (unsigned long)frame, fps,
                      fps * EMULATED_FRAMES_PER_PAINT,
                      100.0f * fps * EMULATED_FRAMES_PER_PAINT / 60.606f,
                      (unsigned long)(emul_us / 30u),
                      (unsigned)g_system.rotation);
        emul_us = 0; push_us = 0;
    }
}
