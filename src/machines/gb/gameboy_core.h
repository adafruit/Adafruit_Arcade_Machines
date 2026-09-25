// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// The Game Boy machine's only door into the vendored core (core/: Peanut-GB
// + minigb_apu, see core/VENDORED.md).
//
// gameboy_core.c is the ONE translation unit that includes peanut_gb.h and
// minigb_apu.c.inc. Everything else in this machine talks to the core
// through this small C API, for two reasons:
//   * Peanut-GB is header-only and must be compiled exactly once, with its
//     feature macros and audio hooks defined before the include.
//   * Its names (gb_init, gb_run_frame, struct gb_s, JOYPAD_*...) stay out of
//     the rest of the library. The machine's own API is gameboy_*.
#ifndef GAMEBOY_CORE_H
#define GAMEBOY_CORE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAMEBOY_LCD_W 160
#define GAMEBOY_LCD_H 144

// minigb_apu's own generation rate -- deliberately NOT the 22050 Hz the
// board plays at. minigb produces one Game Boy frame of audio per call,
// AUDIO_SAMPLE_RATE / 59.7275 samples rounded down: 368 at 22000 Hz. This
// machine runs one Game Boy frame per 60 Hz display frame, so that is
// 368 x 60 = 22080 samples/s against the board's 22050 -- a surplus of
// 30/s, half a sample per frame, which gameboy_audio's one-sample-per-frame
// correction absorbs. At 22050 the count would be 369, a 90/s surplus that
// the correction could not keep up with. The cost is pitch: about +0.2%
// from the rate, plus the +0.46% of running 59.73 Hz frames at 60 Hz --
// roughly 12 cents, below what anyone hears on Tetris.
#define GAMEBOY_APU_RATE 22000
// Upper bound on samples per frame, for sizing buffers. gameboy_core.c
// checks at compile time that minigb's per-frame count fits.
#define GAMEBOY_APU_MAX_SAMPLES 400

// Joypad bits, 1 = pressed. (The core stores them active-low; the
// conversion happens inside gameboy_core.c.) Values match Peanut-GB's
// JOYPAD_* defines, checked there at compile time.
#define GAMEBOY_PAD_A      0x01
#define GAMEBOY_PAD_B      0x02
#define GAMEBOY_PAD_SELECT 0x04
#define GAMEBOY_PAD_START  0x08
#define GAMEBOY_PAD_RIGHT  0x10
#define GAMEBOY_PAD_LEFT   0x20
#define GAMEBOY_PAD_UP     0x40
#define GAMEBOY_PAD_DOWN   0x80

typedef enum {
    GAMEBOY_CORE_OK = 0,
    GAMEBOY_CORE_BAD_CHECKSUM,    // the header check the real boot ROM does
    GAMEBOY_CORE_UNSUPPORTED,     // a cartridge type Peanut-GB doesn't handle
} gameboy_core_status_t;

// Binds the core to a ROM already in memory (the pointer is kept, not
// copied) and resets it. `rom` must stay valid. It may be in PSRAM: the
// first 16 KB (bank 0, which every game uses constantly) is copied into an
// SRAM mirror here, and only switchable-bank reads go to `rom` itself.
gameboy_core_status_t gameboy_core_init(const uint8_t *rom, uint32_t rom_size);

// The cartridge's 16-character title from its header, NUL-terminated.
const char *gameboy_core_title(void);

// Frame stepping, split so the machine can interleave scanline output with
// emulation: call frame_begin(), then step() until it returns true.
// step() runs at most `max_instructions` CPU instructions and returns true
// as soon as the frame is complete. The core sets its frame flag every
// 70224 cycles even while the game has the LCD switched off, so a frame
// always ends.
void gameboy_core_frame_begin(void);
bool gameboy_core_step(uint32_t max_instructions);

// Double-buffered framebuffer. The core draws each frame into the back
// buffer; swap() makes it the front. Pixels are shade indices 0 (lightest)
// to 3 (darkest), after the game's own palette registers.
typedef uint8_t gameboy_row_t[GAMEBOY_LCD_W];
const gameboy_row_t *gameboy_core_front(void);
void gameboy_core_swap(void);

void gameboy_core_set_pad(uint8_t pressed);

// Generates one Game Boy frame of audio, mixed to mono, into `out`, and
// returns the sample count (at most GAMEBOY_APU_MAX_SAMPLES).
uint32_t gameboy_core_audio_frame(int16_t *out);

// Invalid opcodes/accesses the core has reported since init. It carries on
// after each one; the count is for a heartbeat.
uint32_t gameboy_core_error_count(void);

// With GAMEBOY_CORE_PROFILE defined: the longest single core call and how
// many took over 1 ms, since the previous call. Zeros otherwise.
void gameboy_core_take_step_profile(uint32_t *max_us, uint32_t *over_1ms);

#ifdef __cplusplus
}
#endif

#endif
