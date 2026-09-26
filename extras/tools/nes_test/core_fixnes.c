// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// fixNES (cores/fixnes) behind the spike harness, built as its libretro
// front end builds it (-D__LIBRETRO__): nesEmuLoadGame() to load,
// nesEmuMainLoop() for one frame, then apuUpdate() to drain the audio.
#include <stdio.h>
#include "harness.h"

extern int nesEmuLoadGame(const char *filename);
extern void nesEmuMainLoop(void);
extern bool apuUpdate(void);
extern uint8_t *apuGetBuf(void);
extern uint32_t apuGetBufSize(void);
extern uint8_t inValReads[8];
extern uint8_t *emuPrgRAM;
extern uint32_t emuPrgRAMsize;
extern uint16_t textureImage[]; // RGB565, 256x240 (the default, not COL_32BIT)

// What the libretro front end would supply.
int audioUpdate(void) {
    (void)apuGetBuf(); // the samples are made; the harness has no use for them
    (void)apuGetBufSize();
    return 1;
}
FILE *doOpenFDSBIOS(void) { return NULL; }

static bool load(const char *path, uint8_t *rom, size_t size) {
    (void)rom; (void)size; // fixNES opens the file itself
    return nesEmuLoadGame(path) == 0;
}

static void set_pad(uint8_t bits) {
    for (int i = 0; i < 8; i++) inValReads[i] = (bits >> i) & 1; // same order as PAD_*
}

static void frame(void) {
    nesEmuMainLoop();
    apuUpdate();
}

static uint8_t peek_prg_ram(uint16_t a) {
    const uint32_t off = (uint32_t)a - 0x6000u;
    return (emuPrgRAM && off < emuPrgRAMsize) ? emuPrgRAM[off] : 0;
}

static void rgb(uint8_t *out) {
    for (int i = 0; i < 256 * 240; i++) {
        const uint16_t c = textureImage[i];
        out[i * 3 + 0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
        out[i * 3 + 1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
        out[i * 3 + 2] = (uint8_t)((c & 31) * 255 / 31);
    }
}

const core_t g_core = { "fixNES", load, set_pad, frame, peek_prg_ram, rgb, NULL, NULL };
