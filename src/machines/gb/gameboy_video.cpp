// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See gameboy_video.h.
#include "gameboy_video.h"

#include "hal/arcade_hal_video.h"

namespace {

// RGB565 for each framebuffer pixel value, layer << 4 | shade (see
// gameboy_core.h); 0x24 entries cover 0x00-0x23. Starts as greys until the
// machine sets a palette.
uint16_t g_colour[0x24] = {
    0xFFFFu, 0xAD55u, 0x52AAu, 0x0000u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0xFFFFu, 0xAD55u, 0x52AAu, 0x0000u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0xFFFFu, 0xAD55u, 0x52AAu, 0x0000u,
};
const uint16_t kBorder   = 0x0000u;

// The picture's size on the canvas: 160x144, or 144x160 when rotated 90.
inline uint32_t pic_w(uint8_t rot) { return (rot & 1u) ? GAMEBOY_LCD_H : GAMEBOY_LCD_W; }
inline uint32_t pic_h(uint8_t rot) { return (rot & 1u) ? GAMEBOY_LCD_W : GAMEBOY_LCD_H; }

} // namespace

void gameboy_video_set_palette(const uint16_t colours[GAMEBOY_LAYERS][4]) {
    for (int layer = 0; layer < GAMEBOY_LAYERS; layer++)
        for (int s = 0; s < 4; s++) g_colour[(layer << 4) | s] = colours[layer][s];
}

void gameboy_video_fill_scanline(uint16_t *buf, uint16_t colour) {
    for (uint32_t x = 0; x < HAL_VIDEO_WIDTH; x++) buf[x] = colour;
}

void gameboy_video_render_scanline(uint32_t canvas_y, uint16_t *buf,
                                   const gameboy_row_t *fb,
                                   uint8_t rotation, bool mirror) {
    const uint8_t rot = rotation & 3u;
    const uint32_t w = pic_w(rot), h = pic_h(rot);
    const uint32_t x0 = (HAL_VIDEO_WIDTH - w) / 2u;
    const uint32_t y0 = (HAL_VIDEO_HEIGHT - h) / 2u;

    // Every pixel of the line is written: the board hands back recycled
    // buffers still holding the last frame, so anything left unwritten
    // shows up as garbage on the real screen (DEVNOTES #100).
    if (canvas_y < y0 || canvas_y >= y0 + h) {
        gameboy_video_fill_scanline(buf, kBorder);
        return;
    }
    for (uint32_t x = 0; x < x0; x++) buf[x] = kBorder;
    for (uint32_t x = x0 + w; x < HAL_VIDEO_WIDTH; x++) buf[x] = kBorder;

    // v is the row within the picture; u runs across it. Mirroring flips
    // the finished picture, so it reverses u before the rotation mapping.
    const uint32_t v = canvas_y - y0;
    uint16_t *out = buf + x0;

    switch (rot) {
    case 0: { // upright: picture row v is Game Boy row v
        const uint8_t *src = fb[v];
        if (!mirror) for (uint32_t u = 0; u < w; u++) out[u] = g_colour[src[u]];
        else         for (uint32_t u = 0; u < w; u++) out[u] = g_colour[src[w - 1u - u]];
        break;
    }
    case 2: { // 180: picture row v is Game Boy row 143 - v, reversed
        const uint8_t *src = fb[GAMEBOY_LCD_H - 1u - v];
        if (!mirror) for (uint32_t u = 0; u < w; u++) out[u] = g_colour[src[w - 1u - u]];
        else         for (uint32_t u = 0; u < w; u++) out[u] = g_colour[src[u]];
        break;
    }
    case 1: { // 90 CCW: picture (u, v) is Game Boy (x = 159 - v, y = u)
        const uint32_t gx = GAMEBOY_LCD_W - 1u - v;
        for (uint32_t u = 0; u < w; u++) {
            const uint32_t uu = mirror ? (w - 1u - u) : u;
            out[u] = g_colour[fb[uu][gx]];
        }
        break;
    }
    default: { // 3, 90 CW: picture (u, v) is Game Boy (x = v, y = 143 - u)
        const uint32_t gx = v;
        for (uint32_t u = 0; u < w; u++) {
            const uint32_t uu = mirror ? (w - 1u - u) : u;
            out[u] = g_colour[fb[GAMEBOY_LCD_H - 1u - uu][gx]];
        }
        break;
    }
    }
}
