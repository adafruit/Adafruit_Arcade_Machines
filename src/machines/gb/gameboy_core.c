// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// The one translation unit that compiles the vendored Game Boy core. See
// gameboy_core.h for why, and core/VENDORED.md for what is vendored.
//
// Nothing here is linked into an arcade sketch: the library is archived
// (dot_a_linkage), and no arcade game references a symbol in this file.
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "gameboy_core.h"

// --- audio core --------------------------------------------------------
#define MINIGB_APU_AUDIO_FORMAT_S16SYS
#define AUDIO_SAMPLE_RATE GAMEBOY_APU_RATE
#include "core/minigb_apu.h"
#include "core/minigb_apu.c.inc"

// minigb writes AUDIO_SAMPLE_RATE / 59.7275 stereo samples per call into a
// buffer this file sizes from GAMEBOY_APU_MAX_SAMPLES. Clamping the count
// afterwards would not stop an overflow, so the fit is checked here, in
// integer arithmetic the preprocessor can do (59.7275 Hz = 597275 / 10000).
#if (GAMEBOY_APU_RATE * 10000LL / 597275) > GAMEBOY_APU_MAX_SAMPLES
#error "GAMEBOY_APU_MAX_SAMPLES is too small for GAMEBOY_APU_RATE"
#endif

static struct minigb_apu_ctx g_apu;

// Peanut-GB calls these on every APU register access when ENABLE_SOUND is
// set, and declares no prototypes for them, so they must exist before the
// include below.
uint8_t audio_read(uint16_t addr) { return minigb_apu_audio_read(&g_apu, addr); }
void audio_write(uint16_t addr, uint8_t val) { minigb_apu_audio_write(&g_apu, addr, val); }

// --- CPU/PPU core ------------------------------------------------------
#define ENABLE_SOUND 1
#define ENABLE_LCD 1
#include "core/peanut_gb.h"

_Static_assert(GAMEBOY_PAD_A == JOYPAD_A && GAMEBOY_PAD_B == JOYPAD_B &&
               GAMEBOY_PAD_SELECT == JOYPAD_SELECT && GAMEBOY_PAD_START == JOYPAD_START &&
               GAMEBOY_PAD_RIGHT == JOYPAD_RIGHT && GAMEBOY_PAD_LEFT == JOYPAD_LEFT &&
               GAMEBOY_PAD_UP == JOYPAD_UP && GAMEBOY_PAD_DOWN == JOYPAD_DOWN,
               "gameboy_core.h pad bits must match Peanut-GB's JOYPAD_*");
_Static_assert(GAMEBOY_LCD_W == LCD_WIDTH && GAMEBOY_LCD_H == LCD_HEIGHT,
               "gameboy_core.h LCD size must match Peanut-GB's");

static struct gb_s g_gb;
static const uint8_t *g_rom;
static uint32_t g_rom_size;
static char g_title[17];

// Cartridge RAM. Phase 1a (Tetris) needs none; this covers MBC1/MBC3 carts
// that use it, but is NOT persisted -- battery saves arrive in Phase 1b.
static uint8_t g_cart_ram[0x8000];

static gameboy_row_t g_fb[2][GAMEBOY_LCD_H];
static uint8_t g_back = 0;

static uint8_t rom_read(struct gb_s *gb, const uint_fast32_t addr) {
    (void)gb;
    return addr < g_rom_size ? g_rom[addr] : 0xFF;
}

static uint8_t cart_ram_read(struct gb_s *gb, const uint_fast32_t addr) {
    (void)gb;
    return addr < sizeof g_cart_ram ? g_cart_ram[addr] : 0xFF;
}

static void cart_ram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val) {
    (void)gb;
    if (addr < sizeof g_cart_ram) g_cart_ram[addr] = val;
}

// The core reports an invalid opcode or access and carries on; nothing
// sensible can be done mid-frame on a microcontroller, so it is counted and
// the count is available for a heartbeat.
static volatile uint32_t g_errors;
static void core_error(struct gb_s *gb, const enum gb_error_e err, const uint16_t addr) {
    (void)gb; (void)err; (void)addr;
    g_errors++;
}

// Bits 0-1 are the shade after the game's palette registers; the bits
// above say which palette it came from, which a DMG picture doesn't need.
static void lcd_draw_line(struct gb_s *gb, const uint8_t *pixels, const uint_fast8_t line) {
    (void)gb;
    uint8_t *dst = g_fb[g_back][line];
    for (int x = 0; x < GAMEBOY_LCD_W; x++) dst[x] = pixels[x] & 3u;
}

gameboy_core_status_t gameboy_core_init(const uint8_t *rom, uint32_t rom_size) {
    g_rom = rom;
    g_rom_size = rom_size;
    g_errors = 0;
    memset(g_cart_ram, 0xFF, sizeof g_cart_ram);
    memset(g_fb, 0, sizeof g_fb);
    g_back = 0;

    memset(g_title, 0, sizeof g_title);
    for (int i = 0; i < 16 && rom_size > 0x134u + (uint32_t)i; i++) {
        const char c = (char)rom[0x134 + i];
        if (c < 0x20 || c > 0x7E) break;
        g_title[i] = c;
    }

    const enum gb_init_error_e e =
        gb_init(&g_gb, rom_read, cart_ram_read, cart_ram_write, core_error, NULL);
    if (e == GB_INIT_INVALID_CHECKSUM) return GAMEBOY_CORE_BAD_CHECKSUM;
    if (e != GB_INIT_NO_ERROR) return GAMEBOY_CORE_UNSUPPORTED;
    gb_init_lcd(&g_gb, lcd_draw_line);
    minigb_apu_audio_init(&g_apu);
    return GAMEBOY_CORE_OK;
}

const char *gameboy_core_title(void) { return g_title; }

void gameboy_core_frame_begin(void) { g_gb.gb_frame = false; }

bool gameboy_core_step(uint32_t max_instructions) {
    while (max_instructions--) {
        if (g_gb.gb_frame) return true;
        __gb_step_cpu(&g_gb);
    }
    return g_gb.gb_frame;
}

const gameboy_row_t *gameboy_core_front(void) { return g_fb[g_back ^ 1u]; }

void gameboy_core_swap(void) { g_back ^= 1u; }

void gameboy_core_set_pad(uint8_t pressed) { g_gb.direct.joypad = (uint8_t)~pressed; }

uint32_t gameboy_core_audio_frame(int16_t *out) {
    // AUDIO_SAMPLES is computed from floating-point constants, so it isn't
    // an integer constant expression and can't size the array itself.
    static audio_sample_t stereo[2 * GAMEBOY_APU_MAX_SAMPLES];
    const uint32_t n = AUDIO_SAMPLES; // fits: checked by the #if above
    minigb_apu_audio_callback(&g_apu, stereo);
    for (uint32_t i = 0; i < n; i++)
        out[i] = (int16_t)(((int32_t)stereo[2 * i] + stereo[2 * i + 1]) / 2);
    return n;
}

uint32_t gameboy_core_error_count(void) { return g_errors; }
