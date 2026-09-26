// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// The NES console (Phase 2, extras/CONSOLES_PLAN.md): the SD card is the
// cartridge. Put one .nes (iNES) file in /cart; the first found boots.
//
// Core: nofrendo, vendored from retro-go (core/VENDORED.md, GPL-2.0-only),
// wrapped by nes_core.c and stepped a scanline at a time. The ROM is loaded
// into bulk memory (PSRAM) and, if small, copied into SRAM (NES_ROM_SRAM_MAX).
//
// Not yet: battery saves (the core exposes the cart RAM; the Game Boy's save
// path will be generalised for it).
#ifndef NES_MACHINE_H
#define NES_MACHINE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NES_COLOR_ERROR_NO_CARD  0xF800u // red:     no card, or it won't mount
#define NES_COLOR_ERROR_NO_ROM   0xFFE0u // yellow:  mounted, but no .nes in /cart
#define NES_COLOR_ERROR_BAD_CART 0xF81Fu // magenta: bad image, unsupported mapper,
                                         //          too big, or unreadable

#ifndef NES_ROM_MAX
#define NES_ROM_MAX (2u * 1024u * 1024u)
#endif
// ROMs up to this size are copied from PSRAM into SRAM (~0.3 ms a frame
// faster on the Fruit Jam, DEVNOTES #135).
#ifndef NES_ROM_SRAM_MAX
#define NES_ROM_SRAM_MAX (128u * 1024u)
#endif
#define NES_AUDIO_SAMPLE_RATE 22050

typedef enum {
    NES_BOOT_OK = 0,
    NES_BOOT_NO_CARD,          // red
    NES_BOOT_NO_ROM,           // yellow
    NES_BOOT_TOO_BIG,          // magenta
    NES_BOOT_READ_ERROR,       // magenta
    NES_BOOT_BAD_ROM,          // magenta: not an iNES image nofrendo accepts
    NES_BOOT_UNSUPPORTED,      // magenta: a mapper nofrendo doesn't have
    NES_BOOT_NO_MEMORY,        // magenta: no PSRAM, or the core's own allocations
} nes_boot_error_t;

const char *nes_boot_error_text(nes_boot_error_t e);

typedef struct {
    uint8_t rotation;   // 0 = landscape (the console default), 1 = 90 CCW, 2, 3
    bool    mirror_x;   // no button for now: MIRROR cycles the palette
    uint8_t palette;    // nes_core_palette565() index
    bool    stretch;    // 8:7 aspect correction (nes_video.h); starts off
    uint8_t pad;        // NES_BTN_* held
    bool    rotate_prev, palette_prev, stretch_prev;

    // Filled in by nes_load_cart(), for the sketch to report.
    char     cart_name[64];
    uint32_t cart_size;
    int      mapper;
    const char *mapper_name;
    bool     rom_in_sram;
    uint32_t cart_matches;
    uint32_t mount_attempts;
    nes_boot_error_t boot_error;
} nes_system;

void nes_init_system(nes_system *sys);
bool nes_load_cart(nes_system *sys, uint16_t *out_error_color);

// Once per frame, before nes_run_frame(), with buttons already mapped to
// NES meanings. `rotate`, `palette_next` and `stretch` act on their press
// edge (a held button does not cycle).
void nes_input_update(nes_system *sys, bool up, bool down, bool left, bool right,
                      bool a, bool b, bool start, bool select,
                      bool rotate, bool palette_next, bool stretch);

void nes_run_frame(nes_system *sys);
void nes_draw_error_frame(uint16_t color);

// Worst single scanline of emulation since the last call (then reset), us.
uint32_t nes_take_line_us_max(void);

#ifdef __cplusplus
}
#endif

#endif
