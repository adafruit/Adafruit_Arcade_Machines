// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See gameboy_machine.h.
#include "gameboy_machine.h"

#include <string.h>

#include "gameboy_core.h"
#include "gameboy_video.h"
#include "gameboy_audio.h"
#include "console/console_save.h"
#include "gameboy_palette.h"
#include "cart/cart_loader.h"
#include "hal/arcade_hal_video.h"
#include "hal/arcade_hal_storage.h"
#include "hal/arcade_hal_input.h"
#include "hal/arcade_hal_memory.h"

// The cartridge ROM, in bulk memory (see GAMEBOY_ROM_MAX). Allocated once,
// at boot.
static uint8_t *g_rom = nullptr;

const char *gameboy_boot_error_text(gameboy_boot_error_t e) {
    switch (e) {
    case GAMEBOY_BOOT_OK:              return "no error";
    case GAMEBOY_BOOT_NO_CARD:         return "RED: no SD card, or it won't mount";
    case GAMEBOY_BOOT_NO_ROM:          return "YELLOW: no .gb file in /cart";
    case GAMEBOY_BOOT_TOO_BIG:         return "MAGENTA: the ROM is bigger than the ROM buffer";
    case GAMEBOY_BOOT_READ_ERROR:      return "MAGENTA: the ROM file could not be read";
    case GAMEBOY_BOOT_BAD_CHECKSUM:    return "MAGENTA: the ROM failed its header checksum";
    case GAMEBOY_BOOT_UNSUPPORTED:     return "MAGENTA: unsupported cartridge type";
    case GAMEBOY_BOOT_NO_BULK_MEMORY:  return "MAGENTA: no PSRAM available for the ROM";
    }
    return "unknown";
}

static bool fail(gameboy_system *sys, gameboy_boot_error_t e, uint16_t *out_error_color) {
    sys->boot_error = e;
    *out_error_color = e == GAMEBOY_BOOT_NO_CARD ? GAMEBOY_COLOR_ERROR_NO_CARD
                     : e == GAMEBOY_BOOT_NO_ROM  ? GAMEBOY_COLOR_ERROR_NO_ROM
                                                 : GAMEBOY_COLOR_ERROR_BAD_CART;
    return false;
}

void gameboy_init(gameboy_system *sys) {
    memset(sys, 0, sizeof *sys);
    sys->rotation = 0; // consoles boot in landscape (CONSOLES_PLAN.md)
    hal_video_init();
}

bool gameboy_load_cart(gameboy_system *sys, uint16_t *out_error_color) {
    static const char *const kExts[] = { ".gb", nullptr };

    // The buffer comes out of bulk memory; hal_storage has no size query,
    // so it is sized for the largest cartridge rather than this one.
    if (!g_rom) {
        size_t cap = GAMEBOY_ROM_MAX;
        const size_t free_bulk = hal_mem_bulk_free();
        const size_t margin = 64u * 1024u;
        if (free_bulk < cap + margin) cap = free_bulk > margin ? free_bulk - margin : 0;
        g_rom = cap ? (uint8_t *)hal_mem_bulk_alloc(cap) : nullptr;
        if (!g_rom) return fail(sys, GAMEBOY_BOOT_NO_BULK_MEMORY, out_error_color);
        sys->rom_buffer_size = (uint32_t)cap;
    }

    cart_info_t info;
    const cart_status_t st = cart_load(kExts, g_rom, sys->rom_buffer_size, &info);
    memcpy(sys->cart_name, info.name, sizeof sys->cart_name);
    sys->cart_matches = info.matches;
    sys->mount_attempts = info.mount_attempts;

    if (st == CART_NO_STORAGE) return fail(sys, GAMEBOY_BOOT_NO_CARD, out_error_color);
    if (st != CART_OK) {
        hal_storage_unmount();
        return fail(sys, st == CART_NO_ROM   ? GAMEBOY_BOOT_NO_ROM
                       : st == CART_TOO_BIG  ? GAMEBOY_BOOT_TOO_BIG
                                             : GAMEBOY_BOOT_READ_ERROR,
                    out_error_color);
    }
    sys->cart_size = info.size;
    sys->cart_type = info.size > 0x147u ? g_rom[0x147] : 0;
    // The same header check the real boot ROM makes before it will run a
    // cartridge, plus whether the core supports its memory-bank chip.
    const gameboy_core_status_t cs = gameboy_core_init(g_rom, info.size);
    if (cs != GAMEBOY_CORE_OK) {
        hal_storage_unmount();
        return fail(sys, cs == GAMEBOY_CORE_BAD_CHECKSUM ? GAMEBOY_BOOT_BAD_CHECKSUM
                                                          : GAMEBOY_BOOT_UNSUPPORTED,
                    out_error_color);
    }

    // A battery cartridge loads its .sav and keeps storage mounted for later
    // saves (console/console_save.h); anything else never touches the card
    // again.
    const bool battery = gameboy_core_has_battery();
    if (!console_save_init(sys->cart_name, battery ? gameboy_core_save_ram() : nullptr,
                           battery ? gameboy_core_save_size() : 0))
        hal_storage_unmount();
    memcpy(sys->cart_title, gameboy_core_title(), sizeof sys->cart_title);
    sys->gbc_combo = gameboy_palette_gbc_combo(g_rom);
    gameboy_set_palette(sys, GAMEBOY_PALETTE_DEFAULT);

    gameboy_audio_init();
    hal_input_init();
    return true;
}

void gameboy_input_update(gameboy_system *sys,
                          bool up, bool down, bool left, bool right,
                          bool a, bool b, bool start, bool select,
                          bool rotate, bool palette_next) {
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
    if (palette_next && !sys->palette_prev)
        gameboy_set_palette(sys, (uint8_t)((sys->palette + 1u) % GAMEBOY_PALETTE_COUNT));
    sys->rotate_prev = rotate;
    sys->palette_prev = palette_next;
}

void gameboy_set_palette(gameboy_system *sys, uint8_t palette) {
    if (palette >= GAMEBOY_PALETTE_COUNT) palette = GAMEBOY_PALETTE_DEFAULT;
    sys->palette = palette;
    uint16_t colours[GAMEBOY_LAYERS][4];
    gameboy_palette_colours((gameboy_palette_t)palette, g_rom, colours);
    gameboy_video_set_palette(colours);
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
        // TOP UP, don't add one. A batch of running instructions is a few
        // microseconds, so one line per check used to keep pace -- but a
        // halted call can be ~200 us, three lines of display time, and
        // adding one line per check then lost two lines per round trip and
        // drained the queue to 1 on Kirby's load screen (DEVNOTES #128).
        while (hal_video_valid_level() < QUEUE_LOW) emit_line(sys);
    }
    while (hal_video_valid_level() < AUDIO_HEADROOM) emit_line(sys);
    gameboy_audio_frame();
    // One save step at most (a 512-byte sector, ~0.35 ms on the Fruit Jam),
    // after its own top-up: stacked on the audio burst it would otherwise
    // leave the queue only a few lines deep.
    while (hal_video_valid_level() < AUDIO_HEADROOM) emit_line(sys);
    console_save_frame();
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

