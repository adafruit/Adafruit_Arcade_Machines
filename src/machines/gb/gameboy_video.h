// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Game Boy screen -> 320x240 canvas, one canvas scanline at a time.
//
// 1x CENTERED (extras/CONSOLES_PLAN.md, decided 2026-09-24): one Game Boy
// pixel is one canvas pixel, which the Fruit Jam shows as a 2x2 block, so
// the 160x144 picture appears pixel-perfect at 320x288 on the 640x480
// output, with a black border. Scaled modes on STRETCH come later.
//
// This does not use arcade_video_geom: that module maps portrait arcade
// rasters, and its "yoko"/"tate" names would be backwards for a landscape
// console (see the plan's note on rotation names). At 1x the mapping is
// simple enough to do directly here.
#ifndef GAMEBOY_VIDEO_H
#define GAMEBOY_VIDEO_H

#include <stdint.h>
#include <stdbool.h>

#include "gameboy_core.h"
#include "gameboy_palette.h"

#ifdef __cplusplus
extern "C" {
#endif

// Rotation, matching the arcade machines' numbering:
//   0 = landscape, upright monitor (the console default)
//   1 = 90 degrees CCW, 2 = 180 degrees, 3 = 90 degrees CW
// `mirror` flips the finished picture left-right (Pepper's Ghost cabinets).
void gameboy_video_render_scanline(uint32_t canvas_y, uint16_t *buf,
                                   const gameboy_row_t *fb,
                                   uint8_t rotation, bool mirror);

// Sets the 12 colours the picture is drawn in (gameboy_palette_colours()).
// Takes effect from the next scanline rendered.
void gameboy_video_set_palette(const uint16_t colours[GAMEBOY_LAYERS][4]);

// A whole canvas line of one colour, for the boot error screens.
void gameboy_video_fill_scanline(uint16_t *buf, uint16_t colour);

#ifdef __cplusplus
}
#endif

#endif
