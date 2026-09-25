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

// After the vendored code, not before: arch.h pulls in the Pico SDK, whose
// MAX/MIN would otherwise be redefined by minigb_apu.c.inc's own.
#include "arch/arch.h"

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

// Bank 0 in SRAM. Games run their interrupt handlers and most core routines
// from here, so it is read far more than any switchable bank; the rest of
// the ROM may be in PSRAM, which shares the XIP cache with the program.
static uint8_t g_bank0[0x4000];
static char g_title[17];

// Cartridge RAM. Phase 1a (Tetris) needs none; this covers MBC1/MBC3 carts
// that use it, but is NOT persisted -- battery saves arrive in Phase 1b.
static uint8_t g_cart_ram[0x8000];

static gameboy_row_t g_fb[2][GAMEBOY_LCD_H];
static uint8_t g_back = 0;

static uint8_t rom_read(struct gb_s *gb, const uint_fast32_t addr) {
    (void)gb;
    if (addr < sizeof g_bank0) return g_bank0[addr];
    return addr < g_rom_size ? g_rom[addr] : 0xFF;
}

static uint8_t cart_ram_read(struct gb_s *gb, const uint_fast32_t addr) {
    (void)gb;
    return addr < sizeof g_cart_ram ? g_cart_ram[addr] : 0xFF;
}

// Counts writes that CHANGE a byte: games rewrite unchanged values often,
// and those must not trigger a save (gameboy_save.cpp).
static volatile uint32_t g_ram_changes;
static void cart_ram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val) {
    (void)gb;
    if (addr < sizeof g_cart_ram && g_cart_ram[addr] != val) {
        g_cart_ram[addr] = val;
        g_ram_changes++;
    }
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
    memset(g_bank0, 0xFF, sizeof g_bank0);
    memcpy(g_bank0, rom, rom_size < sizeof g_bank0 ? rom_size : sizeof g_bank0);
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

// GAMEBOY_CORE_PROFILE: time every single core call. Off by default, since
// it reads the clock twice per instruction.
static uint32_t g_step_us_max, g_step_long;

// A halted CPU ends the batch early. One running instruction is a few
// microseconds, so a batch of them is short; one HALTED call is up to
// PEANUT_GB_HALT_YIELD_CYCLES of emulated time (core/VENDORED.md), and
// sixteen of those in a row covered a whole frame without the caller ever
// checking the video queue -- which is how Kirby's load screen still
// starved after the core patch had cut every single call to under 230 us.
bool gameboy_core_step(uint32_t max_instructions) {
    while (max_instructions--) {
        if (g_gb.gb_frame) return true;
#ifdef GAMEBOY_CORE_PROFILE
        const uint64_t t0 = ARCADE_TIME_US64();
        __gb_step_cpu(&g_gb);
        const uint32_t us = (uint32_t)(ARCADE_TIME_US64() - t0);
        if (us > g_step_us_max) g_step_us_max = us;
        if (us > 1000u) g_step_long++;
#else
        __gb_step_cpu(&g_gb);
#endif
        if (g_gb.gb_halt) break;
    }
    return g_gb.gb_frame;
}

void gameboy_core_take_step_profile(uint32_t *max_us, uint32_t *over_1ms) {
    *max_us = g_step_us_max; *over_1ms = g_step_long;
    g_step_us_max = 0; g_step_long = 0;
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

// Header byte 0x147 values whose cartridge has a battery-backed save.
bool gameboy_core_has_battery(void) {
    if (g_rom_size <= 0x147u) return false;
    switch (g_bank0[0x147]) {
    case 0x03: case 0x06: case 0x09: case 0x0D: case 0x0F: case 0x10:
    case 0x13: case 0x1B: case 0x1E: case 0x22: case 0xFF:
        return true;
    default:
        return false;
    }
}

uint32_t gameboy_core_save_size(void) {
    size_t n = 0;
    if (gb_get_save_size_s(&g_gb, &n) != 0) return 0;
    return n <= sizeof g_cart_ram ? (uint32_t)n : 0;
}

uint8_t *gameboy_core_save_ram(void) { return g_cart_ram; }

uint32_t gameboy_core_save_changes(void) { return g_ram_changes; }
