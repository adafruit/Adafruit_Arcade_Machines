// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// NES screen -> 320x240 canvas, one canvas scanline at a time.
//
// 1:1. The 256x240 picture fills the canvas vertically, centred with 32
// black columns either side (the Fruit Jam shows it 2x, 512x480 on the
// 640x480 output). Rotated 90 degrees it is 240 wide and would be 256
// tall, 16 more than the canvas, so the leftmost and rightmost 8 NES columns
// are cropped -- the part of the picture a TV's overscan hid, and where
// many games leave scroll garbage. Aspect correction (the NES's 8:7 pixels)
// on STRETCH comes later.
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
                               const uint16_t *palette, uint8_t rotation, bool mirror);

void nes_video_fill_scanline(uint16_t *buf, uint16_t colour);

#ifdef __cplusplus
}
#endif

#endif
