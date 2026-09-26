// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See nes_core.h. The frame loop below is nofrendo's nes_emulate() (core/
// nes/nes.c) split at the scanline, through nofrendo's public functions;
// keep the two in step if the core is re-vendored.
#include "nes_core.h"

#include <string.h>
#include <stdlib.h>

#include "core/nofrendo.h"

_Static_assert(NES_ROW_PITCH == NES_SCREEN_PITCH && NES_ROW_OFFSET == NES_SCREEN_OVERDRAW,
               "nes_core.h's row layout must match nofrendo's");

// nofrendo's frame buffer layout: 8 + 256 + 8 bytes a row (NES_SCREEN_*).
static uint8_t g_buf[2][NES_SCREEN_PITCH * NES_SCREEN_HEIGHT];
static uint8_t g_back = 0;

static nes_t *g_nes;
static uint16_t *g_pal[NES_PALETTE_COUNT];

nes_core_status_t nes_core_init(uint8_t *rom, uint32_t size, uint32_t sample_rate) {
    g_nes = nes_init(SYS_NES_NTSC, (int)sample_rate, false, NULL);
    if (!g_nes) return NES_CORE_NO_MEMORY;
    rom_t *cart = rom_loadmem(rom, size);
    if (!cart) return NES_CORE_BAD_ROM;
    if (nes_insertcart(cart) != 0) return NES_CORE_UNSUPPORTED;
    memset(g_buf, 0, sizeof g_buf);
    g_back = 0;
    return NES_CORE_OK;
}

int nes_core_mapper_number(void) { return g_nes && g_nes->mapper ? g_nes->mapper->number : -1; }
const char *nes_core_mapper_name(void) { return g_nes && g_nes->mapper ? g_nes->mapper->name : "?"; }

void nes_core_set_pad(uint8_t bits) { input_update(0, bits); }

void nes_core_frame_begin(void) {
    // nes_reset() clears the buffer pointer (it is set every frame, as
    // retro-go does), and the frame is drawn into the back buffer.
    nes_setvidbuf(g_buf[g_back]);
}

// One pass of nes_emulate()'s loop: one scanline of CPU time, its PPU line,
// the vblank NMI at line 241, the mapper's hblank hook, and the APU's frame
// counter. Returns true after the frame's last line.
bool nes_core_step_line(void) {
    nes_t *n = g_nes;
    if (n->scanline >= n->scanlines_per_frame) n->scanline = 0;

    // "Running a little bit ahead seems to fix both Battletoads games" --
    // nofrendo's own comment on the 86 - 12 split.
    int elapsed = nes6502_execute(86 - 12);
    ppu_renderline(n->vidbuf, n->scanline, n->vidbuf != NULL);

    if (n->scanline == 241) {
        elapsed += nes6502_execute(6);
        if (n->ppu->ctrl0 & PPU_CTRL0F_NMI) nes6502_nmi();
        if (n->mapper->vblank) n->mapper->vblank(n);
    }
    if (n->mapper->hblank) {
        elapsed += nes6502_execute(86 - elapsed);
        n->mapper->hblank(n);
    }
    n->cycles += n->cycles_per_scanline;
    elapsed += nes6502_execute(n->cycles - elapsed);
    apu_fc_advance(elapsed);
    n->cycles -= elapsed;

    ppu_endline();
    if (++n->scanline < n->scanlines_per_frame) return false;
    n->scanline = 0;
    return true;
}

const uint8_t *nes_core_front_base(void) { return g_buf[g_back ^ 1u]; }

const uint8_t *nes_core_front_row(uint32_t y) {
    return NES_SCREEN_GETPTR(g_buf[g_back ^ 1u], 0, y);
}

void nes_core_swap(void) { g_back ^= 1u; }

uint32_t nes_core_audio_frame(int16_t *out) {
    apu_emulate();
    uint32_t n = (uint32_t)g_nes->apu->samples_per_frame;
    if (n > NES_APU_MAX_SAMPLES) n = NES_APU_MAX_SAMPLES;
    memcpy(out, g_nes->apu->buffer, n * sizeof(int16_t));
    return n;
}

uint32_t nes_core_palette_count(void) { return NES_PALETTE_COUNT; }

const char *nes_core_palette_name(uint32_t n) {
    static const char *const kNames[NES_PALETTE_COUNT] = {
        "Nofrendo", "Composite", "NES Classic", "NTSC", "PVM", "Smooth",
    };
    return n < NES_PALETTE_COUNT ? kNames[n] : "?";
}

const uint16_t *nes_core_palette565(uint32_t n) {
    if (n >= NES_PALETTE_COUNT) return NULL;
    if (!g_pal[n]) g_pal[n] = (uint16_t *)nofrendo_buildpalette((nespal_t)n, 16);
    return g_pal[n];
}

bool nes_core_has_battery(void) {
    return g_nes && g_nes->cart && g_nes->cart->battery && g_nes->cart->prg_ram_banks > 0;
}
uint8_t *nes_core_save_ram(void) { return nes_core_has_battery() ? g_nes->cart->prg_ram : NULL; }
uint32_t nes_core_save_size(void) {
    return nes_core_has_battery() ? (uint32_t)g_nes->cart->prg_ram_banks * ROM_PRG_BANK_SIZE : 0;
}
