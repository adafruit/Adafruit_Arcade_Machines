// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See gameboy_machine.h.
#include "gameboy_machine.h"

#include <string.h>

#include "gameboy_core.h"
#include "gameboy_video.h"
#include "gameboy_audio.h"
#include "cart/cart_loader.h"
#include "hal/arcade_hal_video.h"
#include "hal/arcade_hal_storage.h"
#include "hal/arcade_hal_input.h"

// The cartridge ROM, in SRAM: the core reads it through a callback on every
// opcode fetch, and a flash or PSRAM stall there would cost every frame.
static uint8_t g_rom[GAMEBOY_ROM_MAX];

void gameboy_init(gameboy_system *sys) {
    memset(sys, 0, sizeof *sys);
    sys->rotation = 0; // consoles boot in landscape (CONSOLES_PLAN.md)
    hal_video_init();
}

bool gameboy_load_cart(gameboy_system *sys, uint16_t *out_error_color) {
    static const char *const kExts[] = { ".gb", nullptr };
    cart_info_t info;
    const cart_status_t st = cart_load(kExts, g_rom, sizeof g_rom, &info);
    memcpy(sys->cart_name, info.name, sizeof sys->cart_name);
    sys->cart_matches = info.matches;
    sys->mount_attempts = info.mount_attempts;

    if (st == CART_NO_STORAGE) {
        *out_error_color = GAMEBOY_COLOR_ERROR_NO_CARD;
        return false;
    }
    if (st != CART_OK) {
        hal_storage_unmount();
        *out_error_color = (st == CART_NO_ROM) ? GAMEBOY_COLOR_ERROR_NO_ROM
                                               : GAMEBOY_COLOR_ERROR_BAD_CART;
        return false;
    }
    hal_storage_unmount(); // the card is never touched again after boot

    sys->cart_size = info.size;
    sys->cart_type = info.size > 0x147u ? g_rom[0x147] : 0;
    // The same header check the real boot ROM makes before it will run a
    // cartridge, plus whether the core supports its memory-bank chip.
    if (gameboy_core_init(g_rom, info.size) != GAMEBOY_CORE_OK) {
        *out_error_color = GAMEBOY_COLOR_ERROR_BAD_CART;
        return false;
    }
    memcpy(sys->cart_title, gameboy_core_title(), sizeof sys->cart_title);

    gameboy_audio_init();
    hal_input_init();
    return true;
}

void gameboy_input_update(gameboy_system *sys,
                          bool up, bool down, bool left, bool right,
                          bool a, bool b, bool start, bool select,
                          bool rotate, bool mirror) {
    uint8_t p = 0;
    if (up)     p |= GAMEBOY_PAD_UP;
    if (down)   p |= GAMEBOY_PAD_DOWN;
    if (left)   p |= GAMEBOY_PAD_LEFT;
    if (right)  p |= GAMEBOY_PAD_RIGHT;
    if (a)      p |= GAMEBOY_PAD_A;
    if (b)      p |= GAMEBOY_PAD_B;
    if (start)  p |= GAMEBOY_PAD_START;
    if (select) p |= GAMEBOY_PAD_SELECT;
    sys->pad = p;

    if (rotate && !sys->rotate_prev) sys->rotation = (uint8_t)((sys->rotation + 1u) & 3u);
    if (mirror && !sys->mirror_prev) sys->mirror_x = !sys->mirror_x;
    sys->rotate_prev = rotate;
    sys->mirror_prev = mirror;
}

// --- Frame loop: emulation interleaved with scanline output ---------------
//
// The DVI queue holds only 32 scanlines, about 2.2 ms of slack, so a frame
// can't be emulated first and drawn afterwards: the display would run dry
// while the CPU works (the arcade machines interleave for the same reason,
// e.g. invaders_run_frame()).
//
// The arcade machines slice by their CPU's cycle count. Peanut-GB keeps no
// running cycle count, so this machine lets the QUEUE decide instead: step
// the CPU in small batches, and whenever fewer than QUEUE_LOW lines are
// waiting, draw the next one. That holds whatever the game is doing --
// halted, LCD off, busy -- because it never depends on the Game Boy's own
// timing.
//
// What is drawn is always the last COMPLETE frame (double buffer). When a
// frame finishes, the swap waits for the display to reach canvas line 0, so
// a picture never changes mid-frame. Waiting for that boundary is also what
// paces emulation to the display: one Game Boy frame per 60 Hz display
// frame, the same lock the arcade machines have.
#define STEP_BATCH 16u
#define QUEUE_LOW  16u

// AUDIO GENERATION IS ONE BURST, and nothing is drawn during it: minigb
// produces a whole frame of samples in a single call. The first hardware run
// measured that call at up to 1.23 ms during the title music, about 18
// scanlines' worth, against a queue this loop only kept 16 deep. The
// heartbeat showed it: `starve 4, minq 1/32` while `work` sat at 10 ms of
// 16.7 -- the uneven-burst failure DEVNOTES #35/#65 describe, which a frame
// total cannot see. So the queue is topped up to AUDIO_HEADROOM lines
// immediately before the burst, leaving ~10 lines of slack at its end.
#define AUDIO_HEADROOM 28u

static uint32_t g_line = 0;
static bool g_swap_pending = false;

static void emit_line(const gameboy_system *sys) {
    if (g_line == 0 && g_swap_pending) {
        gameboy_core_swap();
        g_swap_pending = false;
    }
    uint16_t *buf = hal_video_acquire_scanline();
    gameboy_video_render_scanline(g_line, buf, gameboy_core_front(),
                                  sys->rotation, sys->mirror_x);
    hal_video_submit_scanline(buf);
    if (++g_line == HAL_VIDEO_HEIGHT) g_line = 0;
}

void gameboy_run_frame(gameboy_system *sys) {
    gameboy_core_set_pad(sys->pad);
    gameboy_core_frame_begin();
    while (!gameboy_core_step(STEP_BATCH)) {
        if (hal_video_valid_level() < QUEUE_LOW) emit_line(sys);
    }
    while (hal_video_valid_level() < AUDIO_HEADROOM) emit_line(sys);
    gameboy_audio_frame();
    g_swap_pending = true;
    while (g_swap_pending) emit_line(sys);
}

void gameboy_draw_error_frame(uint16_t color) {
    for (uint32_t i = 0; i < HAL_VIDEO_HEIGHT; i++) {
        uint16_t *buf = hal_video_acquire_scanline();
        gameboy_video_fill_scanline(buf, color);
        hal_video_submit_scanline(buf);
    }
}

uint32_t gameboy_core_errors(void) { return gameboy_core_error_count(); }
