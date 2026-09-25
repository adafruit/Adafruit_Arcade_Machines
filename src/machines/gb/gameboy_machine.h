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

// Largest cartridge ROM held in SRAM. Phase 1a (Tetris, 32 KB) needs far
// less; 256 KB also covers bank-switched carts that need no battery save,
// such as blargg's 64 KB cpu_instrs. Bigger carts need PSRAM (Phase 1b).
#ifndef GAMEBOY_ROM_MAX
#define GAMEBOY_ROM_MAX (256u * 1024u)
#endif

typedef struct {
    uint8_t rotation;  // 0 = landscape (the console default), 1 = 90 CCW,
                       // 2 = 180, 3 = 90 CW -- the arcade machines' numbering
    bool    mirror_x;  // left-right flip, for Pepper's Ghost cabinets
    uint8_t pad;       // GAMEBOY_PAD_* bits currently held
    bool    rotate_prev, mirror_prev; // edge detection for the meta buttons

    // Filled in by gameboy_load_cart(), for the sketch to report.
    char     cart_name[64];
    char     cart_title[17];
    uint32_t cart_size;
    uint8_t  cart_type;    // header byte 0x147 (0x00 = ROM only)
    uint32_t cart_matches; // candidate files found in /cart
    uint32_t mount_attempts; // which try mounted the card (see cart_loader)
} gameboy_system;

// Game-state defaults and hal_video_init() (buffers only; it does not start
// the display and does not touch storage).
void gameboy_init(gameboy_system *sys);

// Mounts storage, loads the cartridge, validates it, starts audio and
// input, unmounts. On failure returns false and the error colour to show.
bool gameboy_load_cart(gameboy_system *sys, uint16_t *out_error_color);

// Called once per frame by the sketch, before gameboy_run_frame(), with the
// buttons already mapped to Game Boy meanings. ROTATE and MIRROR act on
// their press edge, so a held button does not cycle.
void gameboy_input_update(gameboy_system *sys,
                          bool up, bool down, bool left, bool right,
                          bool a, bool b, bool start, bool select,
                          bool rotate, bool mirror);

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
