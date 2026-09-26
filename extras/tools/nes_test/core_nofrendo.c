// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// nofrendo (retro-go's copy, cores/nofrendo) behind the spike harness.
#include <stdio.h>
#include <string.h>
#include "harness.h"
#include "nofrendo.h"

static uint8_t g_vidbuf[NES_SCREEN_PITCH * NES_SCREEN_HEIGHT];
static uint8_t *g_pal; // 256 RGB888 entries (nofrendo_buildpalette)

static bool load(const char *path, uint8_t *rom, size_t size) {
    (void)path;
    if (!nes_init(SYS_NES_NTSC, 22050, false, NULL)) return false;
    nes_setvidbuf(g_vidbuf);
    g_pal = nofrendo_buildpalette(NES_PALETTE_NOFRENDO, 24);
    if (nes_insertcart(rom_loadmem(rom, size)) != 0) return false;
    return true;
}

static void set_pad(uint8_t bits) { input_update(0, bits); }
static void frame(void) { nes_emulate(true); }
static uint8_t peek_prg_ram(uint16_t a) { return mem_getbyte(a); }

static void rgb(uint8_t *out) {
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 256; x++) {
            const uint8_t *c = &g_pal[NES_SCREEN_GETPTR(g_vidbuf, x, y)[0] * 3];
            memcpy(out + (y * 256 + x) * 3, c, 3);
        }
}

static void debug(void) {
    const nes6502_t *c = nes_getptr()->cpu; // (nes6502_getcontext is a stub here)
    printf("    CPU: PC %04X, %s; bytes at PC: %02X %02X %02X\n", (unsigned)c->pc_reg,
           c->jammed ? "JAMMED" : "running", mem_getbyte(c->pc_reg), mem_getbyte(c->pc_reg + 1),
           mem_getbyte(c->pc_reg + 2));
}

const core_t g_core = { "nofrendo", load, set_pad, frame, peek_prg_ram, rgb, debug };
