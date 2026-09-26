// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// NES screen -> 320x240 canvas, one canvas scanline at a time.
//
// 1:1 BY DEFAULT. The 256x240 picture fills the canvas vertically, centred
// with 32 black columns either side (the Fruit Jam shows it 2x, 512x480 on
// the 640x480 output). Rotated 90 degrees it is 240 wide and would be 256
// tall, 16 more than the canvas, so the leftmost and rightmost 8 NES columns
// are cropped -- the part of the picture a TV's overscan hid, and where
// many games leave scroll garbage.
//
// ASPECT CORRECTION (`stretch`, Button 1 like the arcade games). A NES pixel
// is 8:7, a little wider than tall, so 1:1 square pixels make the picture
// look narrow. Corrected, the 256 columns span 292 canvas pixels (256 x 8/7),
// still inside the 320-wide canvas. Rotated 90, the NES's width runs up the
// screen and is already cropped to fit, so the other axis is narrowed
// instead: the 240 rows span 210 canvas pixels (240 x 7/8), the same
// proportions. Both repeat or drop whole pixels (nearest neighbour, sampled
// at pixel centres), so some columns are one canvas pixel wider than others.
// The 1:1 path never goes through these tables: routing the unstretched
// path through a lookup cost Donkey Kong 1.6 ms (arcade_video_geom.h).
//
// Rotation numbering matches the arcade machines and the Game Boy:
// 0 landscape (the console default), 1 = 90 CCW, 2 = 180, 3 = 90 CW;
// `mirror` flips the finished picture left-right.
#ifndef NES_VIDEO_H
#define NES_VIDEO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// `palette` is 256 RGB565 entries (nes_core_palette565()).
void nes_video_render_scanline(uint32_t canvas_y, uint16_t *buf, const uint8_t *front_base,
                               const uint16_t *palette, uint8_t rotation, bool mirror,
                               bool stretch);

#define NES_VIDEO_STRETCH_W 292 // 256 x 8/7, landscape
#define NES_VIDEO_STRETCH_H 210 // 240 x 7/8, the rotated picture's width

void nes_video_fill_scanline(uint16_t *buf, uint16_t colour);

#ifdef __cplusplus
}
#endif

#endif
