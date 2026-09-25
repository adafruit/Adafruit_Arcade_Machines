// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See gameboy_palette.h.
#include "gameboy_palette.h"

#include "gameboy_palette_gbc.h"

namespace {

// RGB565, lightest first. Greys are 255, 170, 85, 0, unchanged from the
// original renderer, so the host regression images stay bit-identical.
const uint16_t kGreys[4] = { 0xFFFFu, 0xAD55u, 0x52AAu, 0x0000u };

// SameBoy's GB_PALETTE_DMG and GB_PALETTE_MGB (Core/display.c, Expat
// licence), as RGB888, lightest first. SameBoy lists them darkest first
// plus a fifth "LCD off" colour, which the renderer doesn't use.
const uint8_t kDmgGreen[4][3] = {
    { 0xC6, 0xDE, 0x8C }, { 0x84, 0xA5, 0x63 }, { 0x39, 0x61, 0x39 }, { 0x08, 0x18, 0x10 },
};
const uint8_t kPocket[4][3] = {
    { 0xC2, 0xCE, 0x93 }, { 0x81, 0x8D, 0x66 }, { 0x3A, 0x4C, 0x3A }, { 0x07, 0x10, 0x0E },
};

inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

// The Game Boy Color's BGR555 as RGB565, uncorrected: green gets its low
// bit from its top bit so full intensity stays full.
inline uint16_t bgr555_to_rgb565(uint16_t c) {
    const uint16_t r = c & 0x1Fu, g = (c >> 5) & 0x1Fu, b = (c >> 10) & 0x1Fu;
    return (uint16_t)((r << 11) | (((g << 1) | (g >> 4)) << 5) | b);
}

} // namespace

const char *gameboy_palette_name(gameboy_palette_t p) {
    switch (p) {
    case GAMEBOY_PALETTE_DMG_GREEN: return "DMG green";
    case GAMEBOY_PALETTE_GREYS:     return "Greys";
    case GAMEBOY_PALETTE_POCKET:    return "Pocket";
    case GAMEBOY_PALETTE_GBC:       return "Game Boy Color";
    default:                        return "?";
    }
}

// The boot ROM's own lookup (SameBoy cgb_boot.asm, GetPaletteIndex): only
// Nintendo's games -- old licensee 0x01, or 0x33 and new licensee "01" --
// are looked up. The first entry whose sum matches wins, except that
// entries from kGbcFirstDuplicate on must also match the title's 4th letter.
uint8_t gameboy_palette_gbc_combo(const uint8_t *rom) {
    const uint8_t old_lic = rom[0x14B];
    const bool nintendo = old_lic == 0x01 ||
                          (old_lic == 0x33 && rom[0x144] == '0' && rom[0x145] == '1');
    if (!nintendo) return 0;

    uint8_t sum = 0;
    for (unsigned i = 0x134; i <= 0x143; i++) sum = (uint8_t)(sum + rom[i]);

    for (unsigned i = 0; i < sizeof kGbcTitleSums; i++) {
        if (kGbcTitleSums[i] != sum) continue;
        if (i >= kGbcFirstDuplicate &&
            (uint8_t)kGbcDupLetter[i - kGbcFirstDuplicate] != rom[0x137]) continue;
        return kGbcComboForSum[i];
    }
    return 0;
}

void gameboy_palette_colours(gameboy_palette_t p, const uint8_t *rom,
                             uint16_t out[GAMEBOY_LAYERS][4]) {
    if (p == GAMEBOY_PALETTE_GBC) {
        const uint8_t *combo = kGbcCombos[gameboy_palette_gbc_combo(rom)];
        for (int layer = 0; layer < GAMEBOY_LAYERS; layer++)
            for (int s = 0; s < 4; s++)
                out[layer][s] = bgr555_to_rgb565(kGbcColours[combo[layer] + s]);
        return;
    }
    uint16_t c[4];
    for (int s = 0; s < 4; s++) {
        switch (p) {
        case GAMEBOY_PALETTE_DMG_GREEN: c[s] = rgb565(kDmgGreen[s][0], kDmgGreen[s][1], kDmgGreen[s][2]); break;
        case GAMEBOY_PALETTE_POCKET:    c[s] = rgb565(kPocket[s][0], kPocket[s][1], kPocket[s][2]); break;
        default:                        c[s] = kGreys[s]; break;
        }
    }
    for (int layer = 0; layer < GAMEBOY_LAYERS; layer++)
        for (int s = 0; s < 4; s++) out[layer][s] = c[s];
}
