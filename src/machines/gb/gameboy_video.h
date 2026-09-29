// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Game Boy screen -> 320x240 canvas, one canvas scanline at a time.
//
// 1x CENTERED (extras/CONSOLES_PLAN.md, decided 2026-09-24): one Game Boy
// pixel is one canvas pixel, which the Fruit Jam shows as a 2x2 block, so
// the 160x144 picture appears pixel-perfect at 320x288 on the 640x480
// output, with a black border. The Fruit Jam's HSTX video can also show
// it at 3x (gameboy_video_scanout_3x(), below), its default; STRETCH
// switches.
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

// --- 3x, straight to a 640x480 output (extras/CONSOLES_PLAN.md) -----------
//
// Each Game Boy pixel an exact 3x3 block: 480x432 upright, 432x480 rotated
// 90, centred. The canvas cannot hold that at an integer scale, so a board
// with a direct scan-out (the Fruit Jam's HSTX video,
// fruitjam_video_set_line_source()) calls gameboy_video_scanout_3x() for
// each output line itself, on its video core.
//
// The machine publishes the frame to show each time it reaches canvas line
// 0 (gameboy_video_publish(), called by gameboy_run_frame()); the scan-out
// takes it at the top of each output frame, so a picture never changes
// mid-frame. Safe because the machine, a queue's depth ahead of the
// display, publishes while the display is still in the bottom rows of the
// previous frame, and the core then redraws that buffer from the top.
void gameboy_video_publish(const gameboy_row_t *front, uint8_t rotation, bool mirror);

// `line` 0..479, `dst` 640 RGB565 pixels, two to a word, first pixel in the
// low half. Runs from RAM and calls nothing.
void gameboy_video_scanout_3x(uint32_t line, uint32_t *dst);

#ifdef __cplusplus
}
#endif

#endif
