// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Game Boy colour palettes: what each of the four shades looks like.
//
// DECIDED 2026-09-25 (extras/CONSOLES_PLAN.md, "Game Boy colour palettes"):
// four palettes, cycled in this order by a button, starting on DMG green:
//
//   DMG green   the original Game Boy's screen
//   Greys       neutral greys (what the port showed until now; dmg-acid2's
//               reference image uses the same four levels)
//   Pocket      the Game Boy Pocket's screen
//   GBC         the colours a Game Boy Color gives an original Game Boy
//               game, chosen from the cartridge title
//
// DMG green and Pocket are SameBoy's measured screen colours
// (Core/display.c, GB_PALETTE_DMG and GB_PALETTE_MGB); the GBC table is
// extracted from SameBoy's boot ROM source (gameboy_palette_gbc.h).
//
// A palette is 12 colours: four shades each for the two sprite palettes
// (OBJ0, OBJ1) and the background, which Peanut-GB tells apart in bits 4-5
// of every pixel. The three DMG-style palettes use the same four colours
// for all three; only GBC tells them apart.
#ifndef GAMEBOY_PALETTE_H
#define GAMEBOY_PALETTE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GAMEBOY_PALETTE_DMG_GREEN = 0,
    GAMEBOY_PALETTE_GREYS,
    GAMEBOY_PALETTE_POCKET,
    GAMEBOY_PALETTE_GBC,
    GAMEBOY_PALETTE_COUNT
} gameboy_palette_t;

// The palette a cartridge starts with.
#ifndef GAMEBOY_PALETTE_DEFAULT
#define GAMEBOY_PALETTE_DEFAULT GAMEBOY_PALETTE_DMG_GREEN
#endif

// Layers, in Peanut-GB's pixel bits 4-5 order.
enum { GAMEBOY_LAYER_OBJ0 = 0, GAMEBOY_LAYER_OBJ1, GAMEBOY_LAYER_BG, GAMEBOY_LAYERS };

const char *gameboy_palette_name(gameboy_palette_t p);

// The Game Boy Color's palette combination for a cartridge, from its header
// (`rom` is at least its first 0x150 bytes): 0, the default, unless the
// game is published by Nintendo and its title is in the table.
uint8_t gameboy_palette_gbc_combo(const uint8_t *rom);

// Fills out[layer][shade] (shade 0 = lightest) with RGB565 colours.
void gameboy_palette_colours(gameboy_palette_t p, const uint8_t *rom,
                             uint16_t out[GAMEBOY_LAYERS][4]);

#ifdef __cplusplus
}
#endif

#endif
