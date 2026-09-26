// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See nes_machine.h.
#include "nes_machine.h"

#include <string.h>
#include <stdlib.h>

#include "nes_core.h"
#include "nes_video.h"
#include "arch/arch.h"
#include "cart/cart_loader.h"
#include "console/console_audio.h"
#include "hal/arcade_hal_video.h"
#include "hal/arcade_hal_storage.h"
#include "hal/arcade_hal_input.h"
#include "hal/arcade_hal_memory.h"

static uint8_t *g_rom = nullptr;
static uint32_t g_line_us_max = 0;

const char *nes_boot_error_text(nes_boot_error_t e) {
    switch (e) {
    case NES_BOOT_OK:          return "no error";
    case NES_BOOT_NO_CARD:     return "RED: no SD card, or it won't mount";
    case NES_BOOT_NO_ROM:      return "YELLOW: no .nes file in /cart";
    case NES_BOOT_TOO_BIG:     return "MAGENTA: the ROM is bigger than the ROM buffer";
    case NES_BOOT_READ_ERROR:  return "MAGENTA: the ROM file could not be read";
    case NES_BOOT_BAD_ROM:     return "MAGENTA: not an iNES image the core accepts";
    case NES_BOOT_UNSUPPORTED: return "MAGENTA: unsupported mapper";
    case NES_BOOT_NO_MEMORY:   return "MAGENTA: out of memory (PSRAM or the core's own)";
    }
    return "unknown";
}

static bool fail(nes_system *sys, nes_boot_error_t e, uint16_t *out_error_color) {
    sys->boot_error = e;
    *out_error_color = e == NES_BOOT_NO_CARD ? NES_COLOR_ERROR_NO_CARD
                     : e == NES_BOOT_NO_ROM  ? NES_COLOR_ERROR_NO_ROM
                                             : NES_COLOR_ERROR_BAD_CART;
    return false;
}

void nes_init_system(nes_system *sys) {
    memset(sys, 0, sizeof *sys);
    sys->rotation = 0; // consoles boot in landscape (CONSOLES_PLAN.md)
    sys->mapper = -1;
    sys->mapper_name = "";
    hal_video_init();
}

bool nes_load_cart(nes_system *sys, uint16_t *out_error_color) {
    static const char *const kExts[] = { ".nes", nullptr };

    size_t cap = NES_ROM_MAX;
    const size_t free_bulk = hal_mem_bulk_free();
    const size_t margin = 64u * 1024u;
    if (free_bulk < cap + margin) cap = free_bulk > margin ? free_bulk - margin : 0;
    uint8_t *bulk = cap ? (uint8_t *)hal_mem_bulk_alloc(cap) : nullptr;
    if (!bulk) return fail(sys, NES_BOOT_NO_MEMORY, out_error_color);

    cart_info_t info;
    const cart_status_t st = cart_load(kExts, bulk, (uint32_t)cap, &info);
    memcpy(sys->cart_name, info.name, sizeof sys->cart_name);
    sys->cart_matches = info.matches;
    sys->mount_attempts = info.mount_attempts;
    if (st == CART_NO_STORAGE) return fail(sys, NES_BOOT_NO_CARD, out_error_color);
    hal_storage_unmount(); // no saves yet: the card is never touched again
    if (st != CART_OK) {
        return fail(sys, st == CART_NO_ROM  ? NES_BOOT_NO_ROM
                       : st == CART_TOO_BIG ? NES_BOOT_TOO_BIG
                                            : NES_BOOT_READ_ERROR,
                    out_error_color);
    }
    sys->cart_size = info.size;

    g_rom = bulk;
    if (info.size <= NES_ROM_SRAM_MAX) {
        if (uint8_t *sram = (uint8_t *)malloc(info.size)) {
            memcpy(sram, bulk, info.size);
            g_rom = sram;
            sys->rom_in_sram = true;
        }
    }

    const nes_core_status_t cs = nes_core_init(g_rom, info.size, NES_AUDIO_SAMPLE_RATE);
    if (cs != NES_CORE_OK) {
        return fail(sys, cs == NES_CORE_BAD_ROM     ? NES_BOOT_BAD_ROM
                       : cs == NES_CORE_UNSUPPORTED ? NES_BOOT_UNSUPPORTED
                                                    : NES_BOOT_NO_MEMORY,
                    out_error_color);
    }
    sys->mapper = nes_core_mapper_number();
    sys->mapper_name = nes_core_mapper_name();

    console_audio_init(NES_AUDIO_SAMPLE_RATE);
    hal_input_init();
    return true;
}

void nes_input_update(nes_system *sys, bool up, bool down, bool left, bool right,
                      bool a, bool b, bool start, bool select,
                      bool rotate, bool palette_next) {
    uint8_t p = 0;
    if (a)      p |= NES_BTN_A;
    if (b)      p |= NES_BTN_B;
    if (select) p |= NES_BTN_SELECT;
    if (start)  p |= NES_BTN_START;
    if (up)     p |= NES_BTN_UP;
    if (down)   p |= NES_BTN_DOWN;
    if (left)   p |= NES_BTN_LEFT;
    if (right)  p |= NES_BTN_RIGHT;
    sys->pad = p;

    if (rotate && !sys->rotate_prev) sys->rotation = (uint8_t)((sys->rotation + 1u) & 3u);
    if (palette_next && !sys->palette_prev)
        sys->palette = (uint8_t)((sys->palette + 1u) % nes_core_palette_count());
    sys->rotate_prev = rotate;
    sys->palette_prev = palette_next;
}

// --- Frame loop: emulation interleaved with scanline output ---------------
//
// The Game Boy's loop (gameboy_machine.cpp, where each rule is explained),
// with a scanline of NES emulation as the unit of work: step a line (~27 us
// on the Fruit Jam), and whenever fewer than QUEUE_LOW lines are waiting,
// top the display queue up. The last COMPLETE frame is drawn (double
// buffer), and the swap waits for canvas line 0, which also paces emulation
// to one NES frame per display frame.
//
// AUDIO IN PIECES. nofrendo makes a frame of samples in one call, and
// nothing is drawn meanwhile. With the queue topped up to 28 first, SMB3's
// busier music still took it down to 7 of 32 on the Fruit Jam (SMB's: 13),
// so that call was ~1.3 ms, ~20 lines. The frame is now made AUDIO_CHUNK
// samples at a time with a top-up before each piece; the APU's output is
// identical (nes_core.h), which the host harness's WAVs confirm.
#define QUEUE_LOW      16u
#define AUDIO_HEADROOM 28u
#define AUDIO_CHUNK    96u

static uint32_t g_line = 0;
static bool g_swap_pending = false;

static void emit_line(const nes_system *sys) {
    if (g_line == 0 && g_swap_pending) {
        nes_core_swap();
        g_swap_pending = false;
    }
    uint16_t *buf = hal_video_acquire_scanline();
    nes_video_render_scanline(g_line, buf, nes_core_front_base(),
                              nes_core_palette565(sys->palette), sys->rotation, sys->mirror_x);
    hal_video_submit_scanline(buf);
    if (++g_line == HAL_VIDEO_HEIGHT) g_line = 0;
}

void nes_run_frame(nes_system *sys) {
    nes_core_set_pad(sys->pad);
    nes_core_frame_begin();
    for (;;) {
        const uint64_t t0 = ARCADE_TIME_US64();
        const bool done = nes_core_step_line();
        const uint32_t us = (uint32_t)(ARCADE_TIME_US64() - t0);
        if (us > g_line_us_max) g_line_us_max = us;
        while (hal_video_valid_level() < QUEUE_LOW) emit_line(sys);
        if (done) break;
    }
    static int16_t samples[NES_APU_MAX_SAMPLES];
    const uint32_t total = nes_core_audio_samples_per_frame();
    for (uint32_t done = 0; done < total;) {
        const uint32_t n = (total - done < AUDIO_CHUNK) ? total - done : AUDIO_CHUNK;
        while (hal_video_valid_level() < AUDIO_HEADROOM) emit_line(sys);
        nes_core_audio_render(samples + done, n);
        done += n;
    }
    console_audio_push(samples, total);
    g_swap_pending = true;
    while (g_swap_pending) emit_line(sys);
}

void nes_draw_error_frame(uint16_t color) {
    for (uint32_t i = 0; i < HAL_VIDEO_HEIGHT; i++) {
        uint16_t *buf = hal_video_acquire_scanline();
        nes_video_fill_scanline(buf, color);
        hal_video_submit_scanline(buf);
    }
}

uint32_t nes_take_line_us_max(void) {
    const uint32_t v = g_line_us_max;
    g_line_us_max = 0;
    return v;
}
