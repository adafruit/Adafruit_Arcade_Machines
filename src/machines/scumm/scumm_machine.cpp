// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See scumm_machine.h. Upstream's CircuitPython adapter
// (fruitjam-arcade CIRCUITPY/emus/emu_scumm.py) is the reference for every
// choice here that isn't about the board: the folder and marker rules, the
// pointer speeds, the keys.
#include "scumm_machine.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

#include "machines/scumm/core/backend/fj_core.h"
#include "arch/arch.h"
#include "cart/cart_loader.h"
#include "console/console_audio.h"
#include "hal/arcade_hal_video.h"
#include "hal/arcade_hal_storage.h"
#include "hal/arcade_hal_input.h"
#include "hal/arcade_hal_memory.h"

// --- The engine's stack and screen --------------------------------------------
//
// In on-chip RAM (core/backend/fj_arduino.h has why). The stack is
// upstream's size; the engine has used at most ~5.5 KB of it.
#define SCUMM_STACK_BYTES (48u * 1024u)
static uint8_t g_stack[SCUMM_STACK_BYTES] __attribute__((aligned(8)));

extern "C" void *fj_arduino_stack_alloc(size_t n) {
    return n <= sizeof g_stack ? g_stack : nullptr;
}

// The engine's 8-bit screen, likewise; anything bigger (Loom PC Engine's
// 16-bit screen) comes from the arena as upstream does it.
extern "C" {
void *fj_host_calloc(size_t a, size_t b);
void fj_host_free(void *p);
}
static uint8_t g_screen[FJ_SCREEN_W * FJ_SCREEN_H] __attribute__((aligned(4)));
static bool g_screen_used = false;

extern "C" void *fj_arduino_screen_alloc(size_t n) {
    if (n <= sizeof g_screen && !g_screen_used) {
        g_screen_used = true;
        memset(g_screen, 0, n);
        return g_screen;
    }
    return fj_host_calloc(n, 1);
}

extern "C" void fj_arduino_screen_free(void *p) {
    if (p == g_screen) g_screen_used = false;
    else fj_host_free(p);
}

// --- Crash-report phases ----------------------------------------------------

static void (*g_phase_hook)(uint32_t) = nullptr;
void scumm_set_phase_hook(void (*hook)(uint32_t)) { g_phase_hook = hook; }
static inline void phase(uint32_t p) { if (g_phase_hook) g_phase_hook(p); }
// A file request comes from inside the engine's frame; back to that phase
// when it returns, so a later hang isn't blamed on the last read.
struct IoPhase {
    explicit IoPhase(uint32_t p) { phase(p); }
    ~IoPhase() { phase(SCUMM_PHASE_ENGINE); }
};

const char *scumm_phase_name(uint32_t p) {
    switch (p) {
    case SCUMM_PHASE_INPUT:    return "input";
    case SCUMM_PHASE_ENGINE:   return "engine frame";
    case SCUMM_PHASE_IO_OPEN:  return "engine: file open";
    case SCUMM_PHASE_IO_READ:  return "engine: file read";
    case SCUMM_PHASE_IO_WRITE: return "engine: file write";
    case SCUMM_PHASE_IO_SEEK:  return "engine: file seek";
    case SCUMM_PHASE_IO_CLOSE: return "engine: file close";
    case SCUMM_PHASE_AUDIO:    return "audio to the console ring";
    case SCUMM_PHASE_LINES:    return "lines into the display queue";
    }
    return "?";
}

// --- The picture -------------------------------------------------------------
//
// In on-chip RAM, not PSRAM: the board's scan-out reads it from the video
// interrupt, where a PSRAM (XIP cache) miss is too slow (DEVNOTES #150).
static uint16_t g_fb[SCUMM_FB_W * SCUMM_FB_H];

void ARCADE_FAST_FUNC(scumm_video_scanout)(uint32_t line, uint32_t *dst) {
    const uint16_t *src = &g_fb[(line >> 1) * SCUMM_FB_W];
    for (int i = 0; i < SCUMM_FB_W; i++) {
        const uint32_t p = src[i];
        dst[i] = p | (p << 16);
    }
}

void scumm_draw_error_frame(uint16_t color) {
    for (uint32_t y = 0; y < HAL_VIDEO_HEIGHT; y++) {
        uint16_t *buf = hal_video_acquire_scanline();
        for (uint32_t x = 0; x < HAL_VIDEO_WIDTH; x++) buf[x] = color;
        hal_video_submit_scanline(buf);
    }
}

// --- Files -------------------------------------------------------------------
//
// The engine names files inside the game folder and expects the case to
// be ignored ("00.LFL" vs "00.lfl"), so the folder is listed once and
// looked up from that list. Handles are small ints, as fj_io wants.
#define MAX_NAMES 256
#define NAME_LEN  40
#define MAX_HANDLES 4    // hal_storage's open-file limit on both boards

static char (*g_names)[NAME_LEN] = nullptr;
static uint32_t g_nnames = 0;
static hal_file_t *g_files[MAX_HANDLES];
static scumm_system *g_sys = nullptr;
static scumm_stats_t g_stats;

static void add_name(const char *name, void *ctx) {
    (void)ctx;
    if (g_nnames >= MAX_NAMES || strlen(name) >= NAME_LEN) return;
    snprintf(g_names[g_nnames++], NAME_LEN, "%s", name);
}

static int find_name(const char *name) {
    for (uint32_t i = 0; i < g_nnames; i++)
        if (strcasecmp(g_names[i], name) == 0) return (int)i;
    return -1;
}

static void io_path(char *out, size_t n, const char *name) {
    snprintf(out, n, "%s/%s", g_sys->folder, name);
}

static void io_note(uint64_t t0) {
    const uint32_t us = (uint32_t)(ARCADE_TIME_US64() - t0);
    if (us > g_stats.io_us_max) g_stats.io_us_max = us;
}

static int io_open(void *ctx, const char *name, int write) {
    (void)ctx;
    IoPhase io_phase(SCUMM_PHASE_IO_OPEN);
    const uint64_t t0 = ARCADE_TIME_US64();
    int h = 0;
    while (h < MAX_HANDLES && g_files[h]) h++;
    if (h == MAX_HANDLES) return -1;
    char path[160];
    hal_file_t *f = nullptr;
    if (write) {
        io_path(path, sizeof path, name);
        f = hal_storage_create(path);
        if (f && find_name(name) < 0) add_name(name, nullptr);
    } else {
        const int i = find_name(name);
        if (i >= 0) {
            io_path(path, sizeof path, g_names[i]);
            f = hal_storage_open(path);
        }
    }
    g_stats.io_opens++;
    io_note(t0);
    if (!f) { g_stats.io_open_misses++; return -1; }
    g_files[h] = f;
    return h;
}

static hal_file_t *handle(int h) {
    return (h >= 0 && h < MAX_HANDLES) ? g_files[h] : nullptr;
}

static int io_read(void *ctx, int h, void *buf, int len) {
    (void)ctx;
    IoPhase io_phase(SCUMM_PHASE_IO_READ);
    const uint64_t t0 = ARCADE_TIME_US64();
    hal_file_t *f = handle(h);
    const int n = (f && len > 0) ? (int)hal_storage_read(f, buf, (uint32_t)len) : 0;
    g_stats.io_reads++;
    io_note(t0);
    return n;
}

static int io_write(void *ctx, int h, const void *buf, int len) {
    (void)ctx;
    IoPhase io_phase(SCUMM_PHASE_IO_WRITE);
    const uint64_t t0 = ARCADE_TIME_US64();
    hal_file_t *f = handle(h);
    const int n = (f && len > 0) ? (int)hal_storage_write(f, buf, (uint32_t)len) : 0;
    g_stats.io_writes++;
    io_note(t0);
    return n;
}

static int io_seek(void *ctx, int h, int32_t pos) {
    (void)ctx;
    IoPhase io_phase(SCUMM_PHASE_IO_SEEK);
    hal_file_t *f = handle(h);
    g_stats.io_seeks++;
    return (f && pos >= 0 && hal_storage_seek(f, (uint32_t)pos)) ? 0 : -1;
}

static int32_t io_size(void *ctx, int h) {
    (void)ctx;
    hal_file_t *f = handle(h);
    return f ? (int32_t)hal_storage_size(f) : 0;
}

static void io_close(void *ctx, int h) {
    (void)ctx;
    hal_file_t *f = handle(h);
    if (!f) return;
    IoPhase io_phase(SCUMM_PHASE_IO_CLOSE);
    const uint64_t t0 = ARCADE_TIME_US64();
    hal_storage_close(f);
    g_files[h] = nullptr;
    io_note(t0);
}

// The engine's log lines (detection, warnings, error()), as they come.
static void io_log(void *ctx, const char *msg) {
    (void)ctx;
#if defined(ARDUINO)
    const size_t n = strlen(msg);
    Serial.printf("[scumm] %s%s", msg, (n && msg[n - 1] == '\n') ? "" : "\n");
#else
    (void)msg;
#endif
}

static fj_io g_io = { io_open, io_read, io_write, io_seek, io_size, io_close, io_log, nullptr };

// --- Audio -------------------------------------------------------------------
//
// The engine appends ~367 stereo frames a 1/60 s frame to this ring; they
// go to the console ring as mono, the sum halved.
//
// CATCH-UP. The engine mixes exactly 1/60 s of audio per frame, however
// long the frame took, so a slow one (a 30-100 ms redraw, a 270 ms room
// load) drains the console ring and it never refills: the ring's own
// correction adds 3 samples a frame, ~12 s to recover 100 ms, and every
// hiccup in between underruns (the stutter on pans; DEVNOTES #165). So after
// each frame the ring is topped back up to its target with extra samples
// from the engine's mixer (fj_core_mix_extra(), core/scumm-mix-extra.patch),
// at most a frame's worth at a time so the top-up never makes a slow frame
// itself. And the target is deeper than the consoles' default, ~93 ms
// against 35, so a redraw doesn't drain it in the first place.
#define SCUMM_AUDIO_TARGET 2048u   // console_audio's maximum
#define SCUMM_AUDIO_TOPUP  368u    // a frame's worth
#define RING_FRAMES 2048u
static int16_t g_ring[RING_FRAMES * 2];
static uint32_t g_ring_rd = 0;       // frames read, modulo RING_FRAMES
static uint32_t g_samples_seen = 0;

static void drain_audio(void) {
    const uint32_t total = fj_core_samples();
    uint32_t n = total - g_samples_seen;
    g_samples_seen = total;
    g_stats.audio_frames += n;
    if (n > RING_FRAMES) {           // can't happen at ~367 a frame; skip ahead if it does
        g_ring_rd = (g_ring_rd + (n - RING_FRAMES)) % RING_FRAMES;
        n = RING_FRAMES;
    }
    static int16_t mono[CONSOLE_AUDIO_MAX_FRAME];
    while (n) {
        const uint32_t chunk = n < CONSOLE_AUDIO_MAX_FRAME ? n : CONSOLE_AUDIO_MAX_FRAME;
        for (uint32_t i = 0; i < chunk; i++) {
            const int16_t *s = &g_ring[g_ring_rd * 2];
            mono[i] = (int16_t)(((int32_t)s[0] + s[1]) / 2);
            g_ring_rd = (g_ring_rd + 1) % RING_FRAMES;
        }
        console_audio_push(mono, chunk);
        n -= chunk;
    }
}

// --- Boot --------------------------------------------------------------------

const char *scumm_boot_error_text(scumm_boot_error_t e) {
    switch (e) {
    case SCUMM_BOOT_OK:        return "no error";
    case SCUMM_BOOT_NO_CARD:   return "RED: no SD card, or it won't mount";
    case SCUMM_BOOT_NO_MARKER: return "YELLOW: no .scumm marker file in /cart";
    case SCUMM_BOOT_NO_FOLDER: return "YELLOW: the game folder the marker names has no files";
    case SCUMM_BOOT_NO_MEMORY: return "MAGENTA: no PSRAM for the engine's 2 MB arena";
    case SCUMM_BOOT_NO_GAME:   return "MAGENTA: no SCUMM game recognised in the folder";
    case SCUMM_BOOT_ENGINE:    return "MAGENTA: the engine failed to start";
    }
    return "unknown";
}

static bool fail(scumm_system *sys, scumm_boot_error_t e, uint16_t *out_error_color) {
    sys->boot_error = e;
    *out_error_color = e == SCUMM_BOOT_NO_CARD ? SCUMM_COLOR_ERROR_NO_CARD
                     : (e == SCUMM_BOOT_NO_MARKER || e == SCUMM_BOOT_NO_FOLDER)
                         ? SCUMM_COLOR_ERROR_NO_GAME
                         : SCUMM_COLOR_ERROR_ENGINE;
    return false;
}

void scumm_init(scumm_system *sys) {
    memset(sys, 0, sizeof *sys);
    g_sys = sys;
    hal_video_init();
}

bool scumm_load(scumm_system *sys, uint16_t *out_error_color) {
    static const char *const kExts[] = { ".scumm", nullptr };

    // The marker: a path, or empty for /cart/<name>/.
    char marker[96];
    cart_info_t info;
    const cart_status_t st = cart_load(kExts, (uint8_t *)marker, sizeof marker - 1, &info);
    memcpy(sys->marker, info.name, sizeof sys->marker);
    sys->matches = info.matches;
    sys->mount_attempts = info.mount_attempts;
    if (st == CART_NO_STORAGE) return fail(sys, SCUMM_BOOT_NO_CARD, out_error_color);
    if (st == CART_NO_ROM) return fail(sys, SCUMM_BOOT_NO_MARKER, out_error_color);
    marker[st == CART_OK ? info.size : 0] = 0;
    char *p = marker;
    while (*p && isspace((unsigned char)*p)) p++;
    char *e = p + strlen(p);
    while (e > p && (isspace((unsigned char)e[-1]) || e[-1] == '/')) *--e = 0;
    // Upstream's launcher mounts the card at /sd, so its markers say
    // "/sd/scumm/loom"; here the card is the root. Accepting both lets one
    // card work with either.
    if (strncmp(p, "/sd/", 4) == 0) p += 3;
    if (*p == '/') {
        snprintf(sys->folder, sizeof sys->folder, "%s", p);
    } else if (*p) {
        snprintf(sys->folder, sizeof sys->folder, "/cart/%s", p);
    } else {
        char stem[64];
        snprintf(stem, sizeof stem, "%s", sys->marker);
        if (char *dot = strrchr(stem, '.')) *dot = 0;
        snprintf(sys->folder, sizeof sys->folder, "/cart/%s", stem);
    }

    if (!g_names) g_names = (char (*)[NAME_LEN])malloc(MAX_NAMES * NAME_LEN);
    g_nnames = 0;
    if (!g_names || !hal_storage_list_dir(sys->folder, add_name, nullptr) || g_nnames == 0)
        return fail(sys, SCUMM_BOOT_NO_FOLDER, out_error_color);
    sys->files = g_nnames;
    return true;
}

bool scumm_start(scumm_system *sys, const char *music, bool subtitles,
                 uint16_t *out_error_color) {
    uint8_t *arena = (uint8_t *)hal_mem_bulk_alloc(SCUMM_ARENA_BYTES);
    if (!arena) return fail(sys, SCUMM_BOOT_NO_MEMORY, out_error_color);

    const int err = fj_core_init(arena, SCUMM_ARENA_BYTES, &g_io);
    if (err == FJ_ERR_NOGAME) return fail(sys, SCUMM_BOOT_NO_GAME, out_error_color);
    if (err != FJ_OK) return fail(sys, SCUMM_BOOT_ENGINE, out_error_color);
    sys->game = fj_core_game();

    fj_core_config("music", music ? music : "pcspk");
    fj_core_config("subtitles", subtitles ? "true" : "false");
    fj_core_config("talkspeed", "60");

    memset(g_fb, 0, sizeof g_fb);
    fj_core_set_output(g_fb, SCUMM_FB_W, SCUMM_FB_H, 16, 0, SCUMM_PICTURE_Y);
    fj_core_set_audio(g_ring, RING_FRAMES, 0);
    g_ring_rd = 0;
    g_samples_seen = 0;
    console_audio_init(FJ_AUDIO_RATE);
    console_audio_set_target(SCUMM_AUDIO_TARGET);
    hal_input_init();

    sys->x = FJ_SCREEN_W / 2;
    sys->y = FJ_SCREEN_H / 2;
    fj_core_input(sys->x, sys->y, 0);
    sys->running = true;
    return true;
}

// --- Input -------------------------------------------------------------------

void scumm_input_update(scumm_system *sys, uint32_t m) {
    phase(SCUMM_PHASE_INPUT);
    // Speeds in pixels a frame, (start, top), speeding up while held.
    static const uint8_t kSpeeds[3][2] = { { 1, 4 }, { 1, 6 }, { 2, 8 } };
    const uint32_t dirs = SCUMM_PAD_UP | SCUMM_PAD_DOWN | SCUMM_PAD_LEFT | SCUMM_PAD_RIGHT;
    if (m & dirs) {
        const uint8_t *sp = kSpeeds[sys->pointer_speed % 3u];
        const int step = (int)(sp[0] + sys->held / 8u) < sp[1] ? (int)(sp[0] + sys->held / 8u) : sp[1];
        sys->held++;
        if (m & SCUMM_PAD_LEFT)  sys->x -= step;
        if (m & SCUMM_PAD_RIGHT) sys->x += step;
        if (m & SCUMM_PAD_UP)    sys->y -= step;
        if (m & SCUMM_PAD_DOWN)  sys->y += step;
        if (sys->x < 0) sys->x = 0;
        if (sys->x > FJ_SCREEN_W - 1) sys->x = FJ_SCREEN_W - 1;
        if (sys->y < 0) sys->y = 0;
        if (sys->y > FJ_SCREEN_H - 1) sys->y = FJ_SCREEN_H - 1;
    } else {
        sys->held = 0;
    }
    int b = 0;
    if (m & SCUMM_PAD_A) b |= FJ_BTN_LEFT;
    if (m & SCUMM_PAD_B) b |= FJ_BTN_RIGHT;
    fj_core_input(sys->x, sys->y, b);

    // Start and Select fire on release, so pressing both sends neither
    // (upstream's "back to the menu"; there is no menu here).
    const uint32_t both = SCUMM_PAD_START | SCUMM_PAD_SELECT;
    if ((m & both) == both) sys->combo = true;
    const uint32_t released = sys->pad_prev & ~m;
    if (!(m & both)) {
        if (!sys->combo) {
            if (released & SCUMM_PAD_START)  fj_core_key(32, 32);              // space
            if (released & SCUMM_PAD_SELECT) fj_core_key(FJ_KEY_F1 + 4, 319);  // F5
        }
        sys->combo = false;
    }
    const uint32_t pressed = m & ~sys->pad_prev;
    if (pressed & SCUMM_PAD_C)      fj_core_key(FJ_KEY_ESCAPE, 27);
    if (pressed & SCUMM_PAD_START2) fj_core_key(46, 46);                      // "."
    if (pressed & SCUMM_PAD_SAVE)   fj_core_save(SCUMM_QUICK_SLOT);
    if (pressed & SCUMM_PAD_LOAD)   fj_core_load(SCUMM_QUICK_SLOT);
    sys->pad_prev = m;
}

// --- The frame ---------------------------------------------------------------

bool scumm_run_frame(scumm_system *sys) {
    if (sys->running) {
        const uint64_t t0 = ARCADE_TIME_US64();
        phase(SCUMM_PHASE_ENGINE);
        sys->running = fj_core_frame() != 0;
        const uint32_t us = (uint32_t)(ARCADE_TIME_US64() - t0);
        if (us > g_stats.frame_us_max) g_stats.frame_us_max = us;
        g_stats.frame_us_sum += us;
        g_stats.frames++;
        phase(SCUMM_PHASE_AUDIO);
        drain_audio();
        const uint32_t depth = console_audio_depth();
        if (depth < SCUMM_AUDIO_TARGET) {
            const uint32_t want = SCUMM_AUDIO_TARGET - depth;
            const uint32_t n = want < SCUMM_AUDIO_TOPUP ? want : SCUMM_AUDIO_TOPUP;
            fj_core_mix_extra((int)n);
            g_stats.audio_topup += n;
            drain_audio();
        }
    }
    phase(SCUMM_PHASE_LINES);
    // 240 lines into the queue: they pace the frame to the display, and
    // carry the picture on a board with no direct scan-out.
    for (uint32_t y = 0; y < HAL_VIDEO_HEIGHT; y++) {
        uint16_t *buf = hal_video_acquire_scanline();
        memcpy(buf, &g_fb[y * SCUMM_FB_W], SCUMM_FB_W * sizeof(uint16_t));
        hal_video_submit_scanline(buf);
    }
    return sys->running;
}

const char *scumm_engine_error(void) { return fj_core_error(); }

void scumm_take_stats(scumm_stats_t *out) {
    g_stats.arena_used = (uint32_t)fj_core_mem_used();
    g_stats.arena_peak = (uint32_t)fj_core_mem_peak();
    g_stats.stack_used = (uint32_t)fj_core_stack_used();
    g_stats.stack_size = (uint32_t)fj_core_stack_size();
    *out = g_stats;
    g_stats.frame_us_max = g_stats.frame_us_sum = g_stats.frames = 0;
    g_stats.io_us_max = 0;
}
