// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// lrescue_featheresp32 -- Lunar Rescue on an Adafruit Feather ESP32 V2 with the
// 2.4" TFT FeatherWing. The SAMP composition root for this pair: the only
// place that knows both "this game" and "this board".
//
// ~32fps, not 60, and the reason is arithmetic rather than anything
// fixable in this sketch. The wing's SPI pins are not the ESP32's IOMUX
// pins, so the GPIO matrix caps the bus at 40MHz; a 320x240 RGB565 frame is
// 153,600 bytes, which is 30.7ms of clocking that has to happen no matter
// what the CPU is doing. Measured frame time is 31.4ms -- within 2% of that
// ceiling, because the scanlines go out by DMA and the i8080 emulation now
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
#include <machines/lrescue/lrescue_machine.h>
#include <machines/lrescue/lrescue_video.h>
#include <machines/lrescue/lrescue_input.h>
#include <machines/lrescue/lrescue_audio.h>

// Two emulated frames per painted frame -- see lrescue_run_cpu_frames().
// The
// panel cannot reach 60Hz, so the game would otherwise run in slow motion.
#define EMULATED_FRAMES_PER_PAINT 2u

// 60.0368Hz, AND THIS IS THE ONE GAME WHERE GAME_HZ IS NOT THE CABINET'S
// REFRESH RATE. Copying 59.541985f from invaders_featheresp32.ino would be
// the natural thing to do and would be wrong, so: why.
//
// Lunar Rescue runs on the same 8080bw board as Space Invaders, same
// 1,996,800Hz i8080, and its real cabinet rate IS 59.541985Hz. But
// lrescue_machine.cpp's FRAMERATE is 60.0368, and that number is not a
// cabinet measurement -- it is an EMPIRICAL CALIBRATION TO THE FRUIT JAM's
// actual DVI rate. That board has no wall-clock limiter; pacing comes from
// PicoDVI's blocking scanline queue, so the machine's cycle budget was bent
// to match the rate the hardware really achieves, which fixed a ~0.83%
// audio desync. Read that constant's comment before touching this one.
//
// CYCLES_PER_FRAME is derived from it (1996800/60.0368 = 33,260), so the
// two have to agree. Pacing this board at 59.541985 while the machine
// spends 33,260 cycles per frame would advance total_cycles at 1,980,378
// cycles per real second instead of 1,996,800 -- 0.83% SLOW against the
// audio ISR's own real-time-paced clock, which is exactly the desync that
// calibration removed. The speaker events this game synthesises are
// timestamped on that cycle axis, so the symptom would be audio, not speed.
//
// The cost of pacing to 60.0368 is that the game runs 0.83% fast against
// the original cabinet -- which is precisely what the Fruit Jam build has
// shipped since that calibration, so the two boards agree. Fixing that
// properly means giving the machine the true 33,536-cycle budget and
// paying for it on the Fruit Jam, which is a change to a verified board
// and not this port's business.
#define GAME_HZ 60.0368f
#define FRAME_BUDGET_US ((uint32_t)(1000000.0f / (GAME_HZ)) * EMULATED_FRAMES_PER_PAINT)


// Set to 1 to time the transport in isolation at boot. See setup().
#define LRESCUE_ESP32_BENCH 0

static arcade_system g_system;

// Minimum producer lead seen since the last heartbeat. See the comment at
// the print below for what it guards.
static int64_t g_min_lead = INT64_MAX;

// --- TWO CORES: emulation on 0, video on 1 --------------------------------
//
// Emulation overlaps the SPI transfer instead of queueing behind it. This
// is the lightest machine on the board -- an i8080 at 2MHz against Burger
// Time's 6502 plus two AY chips -- so it does not NEED the second core the
// way Burger Time did (86% of arcade speed on one core there). It gets it
// anyway because the pattern is now the default and the headroom is free.
//
// Same shape as invaders_featheresp32, sprite latch and all: there is none.
// This renderer reads live VRAM on demand and keeps no frame cache, so it
// has nothing to tear (see lrescue_run_cpu_frames).
//
// THE HANDSHAKE IS NOT OPTIONAL. Signalling the emulation task without
// waiting for a reply lets the count accumulate whenever core 0 falls
// behind, at which point the task stops blocking, core 0's idle task
// starves, and task_wdt aborts -- which presents as random crashes rather
// than as a slowdown. DEVNOTES #115.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static TaskHandle_t g_emu_task   = NULL;
static TaskHandle_t g_video_task = NULL;

static void emulation_task(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        lrescue_run_cpu_frames(&g_system, 1);
        xTaskNotifyGive(g_video_task);
    }
}

static bool     g_assets_ok = false;
static uint16_t g_error_color = 0;

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("[lrescue-esp32] boot: serial up");

    lrescue_init(&g_system);
    Serial.printf("[lrescue-esp32] boot: lrescue_init done, rotation %u\n",
                  (unsigned)g_system.rotation);

    g_assets_ok = lrescue_load_assets(&g_system, &g_error_color);
    Serial.printf("[lrescue-esp32] boot: assets %s\n",
                  g_assets_ok ? "loaded OK" : "FAILED");
    if (!g_assets_ok) {
        Serial.printf("[lrescue-esp32]   error colour 0x%04X -- red means no SD "
                      "card / would not mount, yellow means mounted but the "
                      "required ROM files were missing\n", g_error_color);
    }
    // Hand the SPI bus to the IDF driver. Must come AFTER asset loading --
    // SdFat reads the ROMs over SPIClass, and the two cannot both own the
    // peripheral. See arch_spi_dma.h.
    hal_video_run();

    // MUST COME AFTER hal_video_run(), AND THAT IS THE POINT. The audio
    // clock has been running since the fill callback was registered during
    // asset loading; the SPI handover and the panel self-test above take
    // 2.46 seconds, and without this call that whole gap becomes a
    // permanent 4.9-million-cycle offset that disables this game's speaker
    // reconstruction. Seeding here, one line before the emulation task
    // starts, is the last moment that offset can still be measured.
    lrescue_sync_audio_clock(&g_system, EMULATED_FRAMES_PER_PAINT);

    // ONLY WHEN THE ASSETS ACTUALLY LOADED. The note about starting this
    // task after asset loading enforced the ORDER and not the FAILURE
    // CASE: with no card it still started, emulating a machine whose CPUs
    // were never reset and whose audio was never initialised, because both
    // of those happen on the success path of lrescue_load_assets().
    //
    // The result is a panic on core 0 and a reboot, so what the panel
    // actually shows is hal_video_run()'s three-flash invert self-test
    // repeating forever rather than the red or yellow error flood. That is
    // why the error screen had never once been seen on this board.
    //
    // ALL FIVE dual-core sketches did this, each by its own route; every
    // one was confirmed on hardware. Burger Time is simply where it was
    // decoded: ay_reset() never ran, so the AY volume-table pointers were
    // still NULL and the first audio slice loaded from address 0
    // (LoadProhibited, EXCVADDR 0). DEVNOTES #123.
    if (g_assets_ok) {
        g_video_task = xTaskGetCurrentTaskHandle();
        xTaskCreatePinnedToCore(emulation_task, "lrescue_emu", 8192, NULL, 2,
                                &g_emu_task, 0);
        for (uint32_t f = 0; f < EMULATED_FRAMES_PER_PAINT; f++) {
            xTaskNotifyGive(g_emu_task);   // prime: loop() opens by waiting
        }
    }

    // Emulation on core 0, priority above the Arduino loop (1) so a long
    // paint cannot delay it, below the audio and input tasks (2) which are
    // tiny and must not jitter. Started AFTER assets load -- it would
    // otherwise run a machine with no ROM in it.

    // TRANSPORT BENCHMARK, one shot. Off by default: it costs ~0.7s of boot
    // and is a tool, not a feature. Turn it on when changing anything about
    // the transport -- it is what found that per-transfer overhead, not
    // pixel throughput, was the ceiling (DEVNOTES #111).
#if LRESCUE_ESP32_BENCH
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

    Serial.printf("[lrescue-esp32] heap free %u, largest block %u, PSRAM %u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                  (unsigned)ESP.getPsramSize());
}

void loop() {
    if (!g_assets_ok) {
        static uint32_t last = 0;
        if (millis() - last > 1000) {
            last = millis();
            Serial.println("[lrescue-esp32] asset load failed -- halted");
        }
        lrescue_draw_error_frame(g_error_color);
        return;
    }

    bool coin   = hal_input_read(HAL_BTN_COIN);
    bool start1 = hal_input_read(HAL_BTN_START1);
    bool start2 = hal_input_read(HAL_BTN_START2);
    bool shoot  = hal_input_read(HAL_BTN_SHOOT);
    bool left   = hal_input_read(HAL_BTN_LEFT);
    bool right  = hal_input_read(HAL_BTN_RIGHT);
    bool rotate = hal_input_read(HAL_BTN_ROTATE);
    bool mirror = hal_input_read(HAL_BTN_MIRROR);   // always false here

    // Left/right/shoot only -- no up/down. The lander descends on its own
    // and the ship is steered horizontally, so this game wires the same
    // three controls its 8080bw sibling does.
    lrescue_input_update(&g_system, coin, start1, start2,
                          left, right, shoot, rotate, mirror);

    // Whole-frame time: emulation and pixel-pushing together, because they
    // are no longer separable. submit_scanline() hands the row to DMA and
    // returns, so the next scanline's i8080 cycles run while it is still on
    // the wire -- the same overlap the Fruit Jam gets from a second core,
    // bought here with a second buffer instead. A number close to 30.7ms
    // means the transfer is the whole cost and the emulator is hidden
    // inside it; a number well above that means something stopped fitting.
    static uint32_t frame = 0, emul_us = 0, push_us = 0, t_prev = 0;
    uint32_t t0 = micros();

    // Sampled EVERY frame because the worst instant is what matters, but
    // printed rarely because the print is not free -- see the heartbeat.
    {
        const int64_t lead = (int64_t)g_system.total_cycles -
                             (int64_t)lrescue_audio_debug_target_cycle();
        if (lead < g_min_lead) g_min_lead = lead;
    }

    // TWO EMULATED FRAMES PER PAINTED FRAME.
    //
    // This board's panel cannot reach 60Hz -- 320x240 RGB565 at 40MHz is
    // 30.7ms of unavoidable clocking -- so the game would otherwise run in
    // slow motion, advancing one frame per display frame. Decoupling them
    // keeps the i8080 at the interrupt rate the real cabinet produced and
    // decimates only the picture, which is what galagino does on the same
    // class of hardware for the same reason.
    //
    // It is nearly free: most of a frame is already spent waiting for the
    // SPI transfer, and the second frame's cycles fit inside that wait.
    // Paint from whatever the emulation core has produced, then ask it for
    // the next EMULATED_FRAMES_PER_PAINT frames. The two overlap: the
    // notifications are sent BEFORE this core starts its next paint, so
    // core 0 emulates while core 1 renders and transmits.
    // Wait for core 0 to finish what it was asked for (bounds the queue),
    // release it for the next frames, then paint while it emulates.
    for (uint32_t f = 0; f < EMULATED_FRAMES_PER_PAINT; f++) {
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    }
    for (uint32_t f = 0; f < EMULATED_FRAMES_PER_PAINT; f++) {
        xTaskNotifyGive(g_emu_task);
    }
    lrescue_render_frame(&g_system);

    // WALL-CLOCK LIMITER, WITH BOUNDED DEBT REPAYMENT -- and the second half
    // of that is specific to this game.
    //
    // Every other sketch here resets the deadline on any overrun, so a heavy
    // frame degrades to "as fast as possible" rather than accumulating a
    // debt it can never repay. That is the right trade when the only thing
    // at stake is a few microseconds of game speed.
    //
    // IT IS THE WRONG TRADE HERE, because the time forgiven is not merely
    // lost -- it walks the emulated clock away from the audio clock, and
    // this game reconstructs its speaker waveform by comparing the two.
    // Measured: a single 17ms heartbeat print, forgiven once every 120
    // frames, held the emulated rate at 59.9fps instead of 60.04 and decayed
    // the producer lead by ~3,400 cycles/second. That crosses the 23,180-
    // cycle floor in seconds, at which point the speaker events all resolve
    // as already-past and the reconstruction silently stops working.
    //
    // So the deadline stays monotonic and a short overrun is repaid out of
    // the following frames' idle time. The escape hatch survives, just with
    // a threshold: only an overrun of more than a whole budget -- a genuine
    // inability to keep up, not a one-off hiccup -- resyncs. That bounds the
    // debt at one frame while making the long-run rate exact.
    // CLOSED LOOP ON THE LEAD, because no constant can be right here.
    //
    // With the debt repaid rather than forgiven, the rate stopped decaying
    // and started CLIMBING instead: +2,270 cycles/second, the emulated clock
    // now running 0.11% fast against the audio clock. Both directions are
    // bugs. Too slow and the lead crosses the 23,180-cycle floor and the
    // speaker reconstruction stops working; too fast and the lead grows
    // without bound, which is pure speaker latency -- ten minutes of play
    // would put the sound two thirds of a second behind the picture, and the
    // event queue's peak depth was already visibly climbing (4 -> 8) as
    // events sat waiting longer.
    //
    // The obvious fix -- nudge GAME_HZ down by 0.11% -- is the wrong kind of
    // fix, and Burger Time's ring already learned why: the consumer's clock
    // is whatever the hardware actually does, not what the constant says.
    // The I2S divider is integer-plus-fraction, so the real sample rate is
    // near 22,050Hz and not exactly it, and it will differ between units.
    // A constant calibrated against this board's crystal is a constant that
    // is wrong on the next one.
    //
    // So the producer is told by the measurement, not by arithmetic: trim
    // the frame budget in proportion to how far the lead sits from where it
    // should. Deadbanded so it does not hunt, and clamped to 100us -- 0.3%
    // of a frame, far below anything a player can feel, and still ~6,000
    // cycles/second of correction authority against a 2,270 cycles/second
    // drift.
    //
    // LEAD_TARGET is 60,000 cycles: 30ms of speaker latency, comfortably
    // clear of the 23,180-cycle floor with room for the burst-then-idle
    // swing of the emulation task on top.
    #define LEAD_TARGET   60000
    #define LEAD_DEADBAND 16000
    #define TRIM_CLAMP_US 100
    const int64_t lead_now = (int64_t)g_system.total_cycles -
                             (int64_t)lrescue_audio_debug_target_cycle();
    int32_t trim_us = 0;
    const int64_t err = lead_now - (int64_t)LEAD_TARGET;
    if (err > LEAD_DEADBAND || err < -LEAD_DEADBAND) {
        // Lead too high means emulation is ahead, so LENGTHEN the frame.
        // Cycles and microseconds are within a factor of two here
        // (1,996,800 cycles/s), so the raw divisor is the whole gain term.
        trim_us = (int32_t)(err / 64);
        if (trim_us >  TRIM_CLAMP_US) trim_us =  TRIM_CLAMP_US;
        if (trim_us < -TRIM_CLAMP_US) trim_us = -TRIM_CLAMP_US;
    }

    // Bounded debt repayment, kept alongside the loop above: the trim
    // corrects a systematic rate mismatch, this absorbs a single hiccup.
    // Only an overrun of more than a whole budget -- a genuine inability to
    // keep up rather than a one-off -- resyncs and forgives the time.
    static uint32_t deadline = 0;
    if (deadline == 0) deadline = micros();
    deadline += (uint32_t)((int32_t)FRAME_BUDGET_US + trim_us);
    int32_t slack = (int32_t)(deadline - micros());
    if (slack > 0)                              delayMicroseconds((uint32_t)slack);
    else if (slack < -(int32_t)FRAME_BUDGET_US) deadline = micros();


    uint32_t total = micros() - t0;

    emul_us += total;
    // EVERY 120 FRAMES, NOT 30, AND THE REASON IS A MEASUREMENT THAT BROKE
    // WHAT IT MEASURED. This line is ~200 bytes at 115200 baud -- about
    // 17ms, which overruns a 33ms frame budget that is already 99% spent.
    // The wall-clock limiter below forgives an overrun rather than
    // accumulating debt, so that one frame's overshoot is simply lost, and
    // at one frame in 30 that dropped the emulated rate from 60.04 to 59.5
    // fps. Harmless in most games. Not here: this game times its speaker
    // waveform on the emulated cycle axis, so a 0.8% rate shortfall walks
    // total_cycles away from the audio clock at ~17,800 cycles/second --
    // and the probe added to WATCH that drift was causing it.
    //
    // At one frame in 120 the cost amortises to ~0.14ms/frame and the drift
    // goes away. If this heartbeat ever grows again, check the lead slope.
    if (++frame % 120u == 0) {
        uint32_t now = millis();
        // The first window spans boot, so its rate is meaningless -- it
        // printed "18% of 60.6Hz", which reads like a fault. Prime the
        // clock and skip it.
        if (t_prev == 0) { t_prev = now; emul_us = 0; push_us = 0; return; }
        float fps = 120000.0f / (float)(now - t_prev);
        t_prev = now;
        // Rotation is in the heartbeat because it is not otherwise
        // observable and it changes what the geometry code is doing: 1 and
        // 3 are tate (the picture fills all 320x240), 0 and 2 are yoko (180
        // columns pillarboxed inside 320). It also makes a stray ROTATE
        // press visible -- GPIO 37 is input-only with no internal pull, so
        // an unwired or floating button line cycles this silently.
        // THE PRODUCER LEAD IS THIS GAME'S EQUIVALENT OF DONKEY KONG'S RING
        // DEPTH, and it is here for the same reason: the two-core split
        // makes the emulated clock bursty, and this game times its speaker
        // waveform against that clock rather than against a sample count.
        //
        // lrescue synthesises the ship/thrust tones by timestamping port
        // writes in i8080 cycles and resolving the speaker level at each
        // audio sample's own cycle position. If the CPU's total_cycles ever
        // falls BEHIND the audio task's target_cycle, the tail of a buffer
        // resolves against emulated time that has not happened yet: the
        // level sticks and then catches up abruptly, which is the crunchy
        // arpeggio DEVNOTES #34/#16 chased on the Fruit Jam.
        //
        // The floor is one I2S buffer: 256 samples x (1,996,800 / 22,050) =
        // ~23,180 cycles. Below that and the artifact is audible. Emulation
        // runs a whole paint period ahead of the painter here, so the lead
        // should sit far above it -- but "should" is what the Fruit Jam
        // thought too, so it is measured.
        uint32_t sp_pushed = 0, sp_dropped = 0, sp_peak = 0, sp_drain_hits = 0;
        lrescue_audio_speaker_debug_stats(&sp_pushed, &sp_dropped,
                                          &sp_peak, &sp_drain_hits);
        // The lead carries a large NEGATIVE constant and that is a startup
        // offset, not drift: g_target_cycle starts counting the moment the
        // fill callback is registered during asset loading, while
        // total_cycles does not move until the emulation task starts a
        // little later. Only the SLOPE says whether the two clocks agree,
        // so the raw pair is printed once to make the offset visible.
        static bool first = true;
        if (first) {
            first = false;
            Serial.printf("[lrescue-esp32] clock epochs: total_cycles=%llu "
                          "target_cycle=%llu (startup offset %lld cyc = %.2fs; "
                          "constant, watch the slope not this)\n",
                          (unsigned long long)g_system.total_cycles,
                          (unsigned long long)lrescue_audio_debug_target_cycle(),
                          (long long)g_min_lead, (double)g_min_lead / 1996800.0);
        }
        Serial.printf("[lrescue-esp32] frame %lu  %.1f fps display  "
                      "%.1f fps emulated (%.0f%% of %.1fHz)  frame %lu us  "
                      "rot %u  lead %lld cyc (floor 23180)  spk drop %lu peak %lu drain %lu\n",
                      (unsigned long)frame, fps,
                      fps * EMULATED_FRAMES_PER_PAINT,
                      100.0f * fps * EMULATED_FRAMES_PER_PAINT / (GAME_HZ), (double)(GAME_HZ),
                      (unsigned long)(emul_us / 120u),
                      (unsigned)g_system.rotation,
                      (long long)g_min_lead, (unsigned long)sp_dropped,
                      (unsigned long)sp_peak, (unsigned long)sp_drain_hits);
        g_min_lead = INT64_MAX;
        (void)sp_pushed;
        emul_us = 0; push_us = 0;
    }
}
