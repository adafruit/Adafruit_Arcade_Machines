// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// The NES core wrapper: the one translation unit that includes the vendored
// nofrendo (core/VENDORED.md). Everything else in the NES machine talks to
// this, so nofrendo's API never leaks past it.
//
// STEPPED BY SCANLINE. nofrendo's own nes_emulate() runs a whole frame,
// ~7 ms on the RP2350 (DEVNOTES #135), without returning; the display queue
// holds ~2.2 ms of picture. So this wrapper runs the same per-scanline loop
// itself, one scanline per nes_core_step_line() call (~27 us), and the
// machine feeds the display between calls.
//
// DOUBLE BUFFERED. The core draws into the back buffer; nes_core_swap()
// makes it the front, and the renderer reads rows of the front buffer.
#ifndef NES_CORE_H
#define NES_CORE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NES_LCD_W 256
#define NES_LCD_H 240

// Controller bits, the NES's own order.
#define NES_BTN_A      0x01
#define NES_BTN_B      0x02
#define NES_BTN_SELECT 0x04
#define NES_BTN_START  0x08
#define NES_BTN_UP     0x10
#define NES_BTN_DOWN   0x20
#define NES_BTN_LEFT   0x40
#define NES_BTN_RIGHT  0x80

typedef enum {
    NES_CORE_OK = 0,
    NES_CORE_BAD_ROM,        // not an iNES image nofrendo accepts
    NES_CORE_UNSUPPORTED,    // a mapper nofrendo doesn't have
    NES_CORE_NO_MEMORY,
} nes_core_status_t;

// Allocates the two frame buffers (hal_mem_fast_alloc) if not yet done;
// false if it can't. nes_core_init() calls it; the machine calls it first,
// so the buffers get on-chip RAM before a small ROM is copied there.
bool nes_core_reserve_buffers(void);

// Binds the core to a ROM already in memory (kept, not copied; it may be in
// PSRAM) and resets it. Call once. `sample_rate` is the audio rate.
nes_core_status_t nes_core_init(uint8_t *rom, uint32_t size, uint32_t sample_rate);

int nes_core_mapper_number(void);
const char *nes_core_mapper_name(void);

void nes_core_set_pad(uint8_t bits);

// A frame: frame_begin(), then step_line() until it returns true (262
// scanlines, NTSC). After the last line, audio_frame() may be called once.
void nes_core_frame_begin(void);

// Whether the frames that follow draw their picture (default true). A board
// that shows only some emulated frames -- the Feather paints every second
// one -- skips the pixel writes of the rest; the PPU still runs each line
// (scroll, sprite 0 hit), as nofrendo's own nes_emulate(false). Swap only
// after a drawn frame.
void nes_core_set_draw(bool draw);
bool nes_core_step_line(void);

// The front buffer: 240 rows of NES_ROW_PITCH bytes, the 256 visible
// palette indices of row y starting at base + y * NES_ROW_PITCH +
// NES_ROW_OFFSET (nofrendo draws 8 bytes of slack either side).
#define NES_ROW_PITCH  (8 + 256 + 8)
#define NES_ROW_OFFSET 8
const uint8_t *nes_core_front_base(void);
const uint8_t *nes_core_front_row(uint32_t y);
void nes_core_swap(void);

// A frame's audio, after the frame's last line: samples_per_frame() mono
// samples, made by any number of render() calls that add up to it. The APU
// carries its whole state (and its filter's memory) from one sample to the
// next, so splitting a frame into pieces gives identical output; the
// machine does, to feed the display between pieces.
#define NES_APU_MAX_SAMPLES 512
uint32_t nes_core_audio_samples_per_frame(void);
void nes_core_audio_render(int16_t *out, uint32_t n);

// RGB565 for each of the 256 index values nofrendo draws with (it repeats
// the 64 NES colours for sprite priority), for palette `n` of
// nes_core_palette_count(), or NULL.
uint32_t nes_core_palette_count(void);
const char *nes_core_palette_name(uint32_t n);
// The palettes as one-word names for the settings file (settings/settings.h),
// nes_core_palette_count() of them: "nofrendo", "composite", "nes-classic",
// "ntsc", "pvm", "smooth".
const char *const *nes_core_palette_keys(void);
const uint16_t *nes_core_palette565(uint32_t n);

// Cartridge RAM, for battery saves (none if the cart has no battery).
bool nes_core_has_battery(void);
uint8_t *nes_core_save_ram(void);
uint32_t nes_core_save_size(void);

#ifdef __cplusplus
}
#endif

#endif
