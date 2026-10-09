// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// BEFORE YOU BUILD -- set Tools > Optimize to "Optimize Even More (-O3)".
// The sketch.yaml beside this file pins it for arduino-cli.

// scumm_fruitjam -- LucasArts SCUMM v3/v4 adventures (Loom, Monkey Island 1
// EGA, Indy 3 EGA, the free demos) on the Adafruit Fruit Jam, through the
// vendored fruitjam-scumm engine (src/machines/scumm/core/VENDORED.md).
// The SCUMM sketch's binary contains GPL-3.0-or-later code.
//
// THE CARD: the game's files in a folder, e.g. /scumm/loom/, and a marker
// in /cart naming it: a text file /cart/loom.scumm holding "/scumm/loom".
// An empty marker means /cart/<marker name>/. No game data comes with this
// library; use your own copy, or ScummVM's free demos.
//
// Controls (the pad is a mouse):
//   D-pad          move the pointer (it speeds up while held)
//   A  D10 SHOOT   left click        B  A2 ACTION2  right click
//   C  A1 ACTION3  Escape (skip a cutscene)
//   Start D6       space (pause)     Select A5 COIN  F5 (the game's save/load screen)
//   Start2 D7      "." (skip the current line of dialogue)
//   Button 1       F5                Button 2 quick save   Button 3 quick load
// A USB pad (Nintendo layout): D-pad, A, B, Start and Select as above, Y
// for Escape and X for "." (FRUITJAM_USB_MAP_SCUMM).
// The game's own save screen wants a typed name, so without a keyboard use
// quick save (slot 1, listed as "Fruit Jam" in the game's load screen).
//
// Boot error screens: RED no card, YELLOW no .scumm marker in /cart or its
// folder is empty, MAGENTA no game recognised or the engine failed.
//
// Core 0: the engine, input, audio. Core 1: hal_video_run() -- drives the
// DVI signal and, on the default HSTX video, scans the picture out itself.
#include <Adafruit_Arcade_Machines.h>
#include <hal/arcade_hal_video.h>
#include <hal/arcade_hal_input.h>
#include <machines/scumm/scumm_machine.h>
#include <machines/scumm/scumm_new.h>   // once, here only: see that file
#include <console/console_audio.h>
#include <settings/settings.h>
#include <boards/fruitjam/board_config_fruitjam.h>
#include <boards/fruitjam/fruitjam_crash.h>
#ifdef SCUMM_PROFILE
// Bring-up: where core 0's time goes inside the engine's frame
// (-DSCUMM_PROFILE; boards/fruitjam/fruitjam_profile.h).
#include <boards/fruitjam/fruitjam_profile.h>
#endif
#if defined(USE_TINYUSB)
#include <pio_usb.h>
#include <boards/fruitjam/usb_input_fruitjam.h>
#endif

#define TAG "scumm"

static scumm_system  g_system;
static volatile bool g_video_ready = false;
static bool          g_ok = false;
static uint16_t      g_error_color = 0;
static bool          g_scanout = false;

// SETTINGS SAVED TO THE CARD: /cart/<marker name>.fruitjam.cfg, the same
// file name upstream uses for its per-game settings.
static const char *const kMusic[] = { "pcspk", "adlib", "none" };
static const char *const kSpeeds[] = { "0", "1", "2" };
static int g_set_music, g_set_subtitles, g_set_speed;

static void print_settings(const char *what) {
    char line[256];
    settings_status_line(what, line, sizeof line);
    Serial.printf("[%s] %s\n", TAG, line);
}

// The sketch's own crash-report phases, after the machine's.
enum { PHASE_SETTINGS = SCUMM_PHASE_SKETCH, PHASE_SERIAL };

static const char *phase_name(uint32_t p) {
    if (p == PHASE_SETTINGS) return "settings";
    if (p == PHASE_SERIAL) return "serial output";
    return scumm_phase_name(p);
}

// The last crash, if the last reset was one (boards/fruitjam/fruitjam_crash.h).
static void print_crash(void) {
    const fruitjam_crash_t *c = fruitjam_crash_report();
    if (c->kind == FRUITJAM_CRASH_FAULT) {
        Serial.printf("[%s] LAST RESET WAS A FAULT (#%lu) on core %lu in %s, frame %lu: "
                      "pc 0x%08lx lr 0x%08lx xpsr 0x%08lx sp 0x%08lx r0 0x%08lx r1 0x%08lx "
                      "r2 0x%08lx r3 0x%08lx r12 0x%08lx cfsr 0x%08lx hfsr 0x%08lx "
                      "mmfar 0x%08lx bfar 0x%08lx\n",
                      TAG, (unsigned long)c->count, (unsigned long)c->core,
                      phase_name(c->phase), (unsigned long)c->frame, (unsigned long)c->pc,
                      (unsigned long)c->lr, (unsigned long)c->xpsr, (unsigned long)c->sp,
                      (unsigned long)c->r0, (unsigned long)c->r1, (unsigned long)c->r2,
                      (unsigned long)c->r3, (unsigned long)c->r12, (unsigned long)c->cfsr,
                      (unsigned long)c->hfsr, (unsigned long)c->mmfar, (unsigned long)c->bfar);
    } else if (c->kind == FRUITJAM_CRASH_STALL) {
        Serial.printf("[%s] LAST RESET WAS A STALL (#%lu): core 0 stopped feeding for %lu ms in %s, "
                      "frame %lu, and core 1's NMI found it at pc 0x%08lx lr 0x%08lx (cfsr 0x%08lx)\n",
                      TAG, (unsigned long)c->count, (unsigned long)FRUITJAM_CRASH_STALL_MS,
                      phase_name(c->phase), (unsigned long)c->frame, (unsigned long)c->pc,
                      (unsigned long)c->lr, (unsigned long)c->cfsr);
        print_stall_probe(c->lr);
    } else if (c->kind == FRUITJAM_CRASH_HANG) {
        Serial.printf("[%s] LAST RESET WAS A HANG (#%lu, watchdog %lu ms, no NMI: a lockup?): core 0 was in %s, frame %lu\n",
                      TAG, (unsigned long)c->count, (unsigned long)FRUITJAM_CRASH_WATCHDOG_MS,
                      phase_name(c->phase), (unsigned long)c->frame);
    }
}

#if defined(USE_TINYUSB)
// The stall probe (fruitjam_crash.h): the USB host's transmit DMA and PIO
// state at the moment of a stall, packed into one word:
//   0-11 DMA words left, 12 busy, 13 enabled, 14 read error, 15 write error,
//   16-20 TX state machine PC, 21-24 TX FIFO level, 25-28 DMA channel,
//   29-31 0b101 (valid)
#include <hardware/dma.h>
#include <hardware/structs/pio.h>
// Read straight from the hardware: Pico PIO USB's own header isn't C++.
// usb_host_fruitjam.cpp puts the host on PIO 2, and the default config
// runs its transmitter on state machine 0 (PIO_SM_USB_TX_DEFAULT).
#define USB_TX_PIO pio2_hw
#define USB_TX_SM  0u
#include <boards/fruitjam/usb_host_fruitjam.h>
static uint32_t __not_in_flash_func(usb_stall_probe)(void) {
    const int ch = fruitjam_usb_host_dma_channel();
    if (ch < 0) return 0;
    const uint32_t ctrl = dma_hw->ch[ch].ctrl_trig;
    const uint32_t left = dma_hw->ch[ch].transfer_count & 0xFFFu;
    const uint32_t pc = USB_TX_PIO->sm[USB_TX_SM].addr & 0x1Fu;
    const uint32_t lvl = (USB_TX_PIO->flevel >> (USB_TX_SM * 8u)) & 0xFu;
    return left | (((ctrl >> 26) & 1u) << 12) | ((ctrl & 1u) << 13) |
           (((ctrl >> 30) & 1u) << 14) | (((ctrl >> 29) & 1u) << 15) |
           (pc << 16) | (lvl << 21) | (((uint32_t)ch & 0xFu) << 25) | (5u << 29);
}
#endif

static void print_stall_probe(uint32_t w) {
    if ((w >> 29) != 5u) return;
    Serial.printf("[%s]   usb tx probe: DMA ch %lu, %lu words left, busy %lu, en %lu, rd_err %lu, "
                  "wr_err %lu; TX SM pc %lu, TX FIFO %lu\n", TAG,
                  (unsigned long)((w >> 25) & 0xFu), (unsigned long)(w & 0xFFFu),
                  (unsigned long)((w >> 12) & 1u), (unsigned long)((w >> 13) & 1u),
                  (unsigned long)((w >> 14) & 1u), (unsigned long)((w >> 15) & 1u),
                  (unsigned long)((w >> 16) & 0x1Fu), (unsigned long)((w >> 21) & 0xFu));
}

// The machine's phase hook: the crash report's note, and the profiler's
// window (the engine and its file requests, not the wait for the display).
static void on_phase(uint32_t p) {
    fruitjam_crash_phase(p);
#ifdef SCUMM_PROFILE
    fruitjam_profile_enable(p >= SCUMM_PHASE_ENGINE && p <= SCUMM_PHASE_IO_CLOSE);
#endif
}

#ifdef SCUMM_PROFILE
static void print_profile(void) {
    static fruitjam_profile_bucket_t top[30];
    uint32_t total = 0, outside = 0;
    const uint32_t n = fruitjam_profile_take(top, 30, &total, &outside);
    uint32_t counted = 0;
    for (uint32_t i = 0; i < n; i++) counted += top[i].count;
    Serial.printf("[%s] profile: %lu samples, %lu outside code; top %lu:", TAG,
                  (unsigned long)total, (unsigned long)outside, (unsigned long)n);
    for (uint32_t i = 0; i < n; i++)
        Serial.printf(" %08lx:%lu", (unsigned long)top[i].addr, (unsigned long)top[i].count);
    Serial.printf("\n");
}
#endif

// The scan-out, with core 1's stall check once a video frame.
static void __not_in_flash_func(scanout)(uint32_t line, uint32_t *dst) {
    if (line == 0) fruitjam_crash_poll_core1();
    scumm_video_scanout(line, dst);
}

static void print_game(void) {
    Serial.printf("[%s] game %s, folder %s (%lu files), marker /cart/%s (%lu in /cart), "
                  "mounted on attempt %lu, picture %s\n",
                  TAG, g_system.game ? g_system.game : "?", g_system.folder,
                  (unsigned long)g_system.files, g_system.marker,
                  (unsigned long)g_system.matches, (unsigned long)g_system.mount_attempts,
                  g_scanout ? "direct scan-out" : "through the canvas");
}

void setup() {
    fruitjam_crash_begin();   // first: reads what the last reset left
    Serial.begin(115200);
    // 252 MHz, and the PSRAM holding the engine's arena retimed to match.
    fruitjam_set_sys_clock_khz(252000);

    scumm_init(&g_system);

    // The card and the game's marker first, then its settings file (named
    // after the marker), then the engine, which reads the music and
    // subtitle settings once, at start.
    g_ok = scumm_load(&g_system, &g_error_color);
    if (g_ok) {
        g_set_music = settings_add_choice("music", kMusic, 3, 0,
                                          "pcspk, adlib (DOS Loom and Indy 3), none");
        g_set_subtitles = settings_add_choice("subtitles", SETTINGS_ON_OFF_NAMES, 2, 1, nullptr);
        g_set_speed = settings_add_choice("pointer", kSpeeds, 3, 0, "pointer speed, 0-2");
        char path[96];
        settings_console_path(g_system.marker, "fruitjam", path, sizeof path);
        settings_begin(path, "SCUMM settings for this game on the Fruit Jam.");
        g_system.pointer_speed = (uint8_t)settings_get(g_set_speed);
        g_ok = scumm_start(&g_system, kMusic[settings_get(g_set_music)],
                           settings_get(g_set_subtitles) != 0, &g_error_color);
    }

#if defined(USE_TINYUSB)
    fruitjam_usb_input_begin(FRUITJAM_USB_MAP_SCUMM);
#endif
    if (g_ok) g_scanout = fruitjam_video_set_line_source(scanout);
    if (g_ok) {
        scumm_set_phase_hook(on_phase);
#if defined(USE_TINYUSB)
        fruitjam_crash_set_stall_probe(usb_stall_probe);
#endif
#ifdef SCUMM_PROFILE
        fruitjam_profile_begin();
        fruitjam_profile_enable(false);
#endif
        fruitjam_crash_start_watchdog();
    }
    g_video_ready = true;
}

void loop() {
    if (!g_ok) {
        scumm_draw_error_frame(g_error_color);
        static uint32_t n = 0;
        if ((n++ % 120u) == 0) {
            Serial.printf("[%s] boot error, %s (mount attempts: %lu, marker %s, folder %s)\n",
                          TAG, scumm_boot_error_text(g_system.boot_error),
                          (unsigned long)g_system.mount_attempts,
                          g_system.marker[0] ? g_system.marker : "-",
                          g_system.folder[0] ? g_system.folder : "-");
            print_crash();
        }
        return;
    }

#if defined(USE_TINYUSB)
    fruitjam_usb_input_poll();
#endif
    uint32_t pad = 0;
    if (hal_input_read(HAL_BTN_UP))      pad |= SCUMM_PAD_UP;
    if (hal_input_read(HAL_BTN_DOWN))    pad |= SCUMM_PAD_DOWN;
    if (hal_input_read(HAL_BTN_LEFT))    pad |= SCUMM_PAD_LEFT;
    if (hal_input_read(HAL_BTN_RIGHT))   pad |= SCUMM_PAD_RIGHT;
    if (hal_input_read(HAL_BTN_SHOOT))   pad |= SCUMM_PAD_A;
    if (hal_input_read(HAL_BTN_ACTION2)) pad |= SCUMM_PAD_B;
    if (hal_input_read(HAL_BTN_ACTION3)) pad |= SCUMM_PAD_C;
    if (hal_input_read(HAL_BTN_START1))  pad |= SCUMM_PAD_START;
    if (hal_input_read(HAL_BTN_COIN) || hal_input_read(HAL_BTN_STRETCH)) pad |= SCUMM_PAD_SELECT;
    if (hal_input_read(HAL_BTN_START2))  pad |= SCUMM_PAD_START2;
    if (hal_input_read(HAL_BTN_ROTATE))  pad |= SCUMM_PAD_SAVE;
    if (hal_input_read(HAL_BTN_MIRROR))  pad |= SCUMM_PAD_LOAD;
    scumm_input_update(&g_system, pad);

    static uint32_t frame_count = 0;
    const uint32_t t0 = micros();
    const bool running = scumm_run_frame(&g_system);
    const uint32_t frame_us = micros() - t0;
    const uint32_t blocked_us = hal_video_take_blocked_us();

    fruitjam_crash_feed(frame_count);
#ifdef TEST_CRASH_FRAME
    // Bring-up check of the crash report: a write to an unmapped address.
    if (frame_count == (TEST_CRASH_FRAME)) *(volatile uint32_t *)0xF0000000u = 1u;
#endif
#ifdef TEST_STALL_FRAME
    // And of the stall check: interrupts off and spin, which only core 1's
    // NMI can interrupt.
    if (frame_count == (TEST_STALL_FRAME)) { (void)save_and_disable_interrupts(); for (;;) {} }
#endif
    fruitjam_crash_phase(PHASE_SETTINGS);
    settings_set(g_set_speed, g_system.pointer_speed);
    settings_frame();
    if (settings_take_saved()) print_settings("saved");

    static bool reported_stop = false;
    if (!running && !reported_stop) {
        reported_stop = true;
        Serial.printf("[%s] engine stopped: %s\n", TAG, scumm_engine_error());
    }

    if ((++frame_count % 60u) == 0) {
        fruitjam_crash_phase(PHASE_SERIAL);
        scumm_stats_t s;
        scumm_take_stats(&s);
        console_audio_stats_t as;
        console_audio_take_stats(&as);
        Serial.printf("[%s] frame %lu, frame %luus (blocked %luus), engine MEAN %luus max %luus, "
                      "arena %lu KB peak %lu KB, stack %lu/%lu, io max %luus opens %lu (miss %lu) "
                      "reads %lu seeks %lu writes %lu, audio %lu frames (topup %lu), ur %lu ov %lu depth %lu, "
                      "starve %lu, minq %lu/%lu, ptr %d,%d, pad 0x%lx%s\n",
                      TAG, (unsigned long)frame_count, (unsigned long)frame_us,
                      (unsigned long)blocked_us,
                      (unsigned long)(s.frames ? s.frame_us_sum / s.frames : 0),
                      (unsigned long)s.frame_us_max,
                      (unsigned long)(s.arena_used / 1024u), (unsigned long)(s.arena_peak / 1024u),
                      (unsigned long)s.stack_used, (unsigned long)s.stack_size,
                      (unsigned long)s.io_us_max, (unsigned long)s.io_opens,
                      (unsigned long)s.io_open_misses, (unsigned long)s.io_reads,
                      (unsigned long)s.io_seeks, (unsigned long)s.io_writes,
                      (unsigned long)s.audio_frames, (unsigned long)s.audio_topup,
                      (unsigned long)as.underruns,
                      (unsigned long)as.overruns, (unsigned long)as.depth,
                      (unsigned long)hal_video_take_starve_count(),
                      (unsigned long)hal_video_take_min_valid_level(),
                      (unsigned long)hal_video_scanbuf_count(), g_system.x, g_system.y,
                      (unsigned long)pad, running ? "" : ", STOPPED");
        if ((frame_count % 600u) == 60u) { print_game(); print_settings("in use"); }
#ifdef SCUMM_PROFILE
        if ((frame_count % 600u) == 0u) print_profile();
#endif
        // A crash report: every heartbeat for the first minute, then with the
        // game line (the boot messages never reach the host).
        if (frame_count <= 3600u || (frame_count % 600u) == 60u) print_crash();
    }
}

void setup1() {
    while (!g_video_ready) {
        tight_loop_contents();
    }
    hal_video_run(); // never returns
}

void loop1() {
}
