// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Game Boy (DMG) machine: the first console (extras/CONSOLES_PLAN.md).
//
// Same shape as the arcade machines -- init, load assets, run one frame,
// draw an error frame -- with one difference that matters: the program
// isn't fixed. Whatever cartridge ROM is in /cart/ on the SD card at power-up
// is the game (see src/cart/cart_loader.h).
//
// Board-agnostic like every machine here: it talks only through src/hal/.
// Button wiring lives in the sketch, which passes plain booleans in.
#ifndef GAMEBOY_MACHINE_H
#define GAMEBOY_MACHINE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Boot error screens. Red and yellow mean what they mean for every arcade
// machine; magenta is the consoles' own "wrong cartridge".
#define GAMEBOY_COLOR_ERROR_NO_CARD  0xF800u // red:     no card, or it won't mount
#define GAMEBOY_COLOR_ERROR_NO_ROM   0xFFE0u // yellow:  mounted, but no .gb in /cart
#define GAMEBOY_COLOR_ERROR_BAD_CART 0xF81Fu // magenta: bad header, unsupported type,
                                             //          too big, or unreadable

// Largest cartridge ROM. The ROM lives in BULK memory (PSRAM: 8 MB on the
// Fruit Jam, 2 MB on the Feather, hal/arcade_hal_memory.h), except for its
// first 16 KB, bank 0, which gameboy_core keeps in SRAM because every game
// runs its interrupt handlers and core routines from it. 4 MB is more than
// any original Game Boy cartridge; on a board with less bulk memory free,
// the buffer is whatever is free, less a margin.
#ifndef GAMEBOY_ROM_MAX
#define GAMEBOY_ROM_MAX (4u * 1024u * 1024u)
#endif

// Why a boot failed, for the sketch to report. The colour alone lumps
// several of these together.
typedef enum {
    GAMEBOY_BOOT_OK = 0,
    GAMEBOY_BOOT_NO_CARD,         // red
    GAMEBOY_BOOT_NO_ROM,          // yellow
    GAMEBOY_BOOT_TOO_BIG,         // magenta: larger than the ROM buffer
    GAMEBOY_BOOT_READ_ERROR,      // magenta: found, but unreadable
    GAMEBOY_BOOT_BAD_CHECKSUM,    // magenta: failed the header check
    GAMEBOY_BOOT_UNSUPPORTED,     // magenta: cartridge type not supported
    GAMEBOY_BOOT_NO_BULK_MEMORY,  // magenta: no PSRAM to hold the ROM
} gameboy_boot_error_t;

const char *gameboy_boot_error_text(gameboy_boot_error_t e);

typedef struct {
    uint8_t rotation;  // 0 = landscape (the console default), 1 = 90 CCW,
                       // 2 = 180, 3 = 90 CW -- the arcade machines' numbering
    bool    mirror_x;  // left-right flip, for Pepper's Ghost cabinets (no
                       // button for now: MIRROR cycles the palette instead)
    uint8_t palette;   // gameboy_palette_t; starts at GAMEBOY_PALETTE_DEFAULT
    uint8_t pad;       // GAMEBOY_PAD_* bits currently held
    bool    rotate_prev, palette_prev; // edge detection for the meta buttons

    // Filled in by gameboy_load_cart(), for the sketch to report.
    char     cart_name[64];
    char     cart_title[17];
    uint32_t cart_size;
    uint8_t  cart_type;    // header byte 0x147 (0x00 = ROM only)
    uint32_t cart_matches; // candidate files found in /cart
    uint32_t mount_attempts; // which try mounted the card (see cart_loader)
    uint8_t  gbc_combo;    // the Game Boy Color palette combination for
                           // this cartridge (0 = the default)
    gameboy_boot_error_t boot_error;
    uint32_t rom_buffer_size;  // bytes of bulk memory given to the ROM
} gameboy_system;

// Game-state defaults and hal_video_init() (buffers only; it does not start
// the display and does not touch storage).
void gameboy_init(gameboy_system *sys);

// Mounts storage, loads the cartridge, validates it, starts audio and
// input, unmounts. On failure returns false and the error colour to show.
bool gameboy_load_cart(gameboy_system *sys, uint16_t *out_error_color);

// Called once per frame by the sketch, before gameboy_run_frame(), with the
// buttons already mapped to Game Boy meanings. `rotate` cycles the
// rotation and `palette_next` the palette, each on its press edge, so a
// held button does not cycle.
void gameboy_input_update(gameboy_system *sys,
                          bool up, bool down, bool left, bool right,
                          bool a, bool b, bool start, bool select,
                          bool rotate, bool palette_next);

// Selects a palette (gameboy_palette_t) for the loaded cartridge.
void gameboy_set_palette(gameboy_system *sys, uint8_t palette);

// Emulates one Game Boy frame and feeds the display while doing it. See
// gameboy_machine.cpp for how emulation and scanline output interleave.
void gameboy_run_frame(gameboy_system *sys);

// One full canvas frame of a solid colour, for the boot error screens.
void gameboy_draw_error_frame(uint16_t color);

// Heartbeat counter: core errors (invalid opcodes/accesses) since boot.
uint32_t gameboy_core_errors(void);

#ifdef __cplusplus
}
#endif

#endif
