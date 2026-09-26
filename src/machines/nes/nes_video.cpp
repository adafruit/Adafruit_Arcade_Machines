// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See nes_video.h.
#include "nes_video.h"

#include "nes_core.h"
#include "hal/arcade_hal_video.h"

namespace {

const uint16_t kBorder = 0x0000u;
const uint32_t kCrop = 8; // NES columns hidden at each side when rotated 90

inline const uint8_t *row(const uint8_t *base, uint32_t y) {
    return base + y * NES_ROW_PITCH + NES_ROW_OFFSET;
}

// Aspect-correction maps, built once: canvas pixel -> NES column (landscape)
// and canvas pixel -> NES row (rotated), sampled at pixel centres so the
// first and last source pixels both appear.
uint8_t g_col[NES_VIDEO_STRETCH_W];
uint8_t g_row[NES_VIDEO_STRETCH_H];
bool g_maps_built = false;

void build_maps() {
    for (uint32_t d = 0; d < NES_VIDEO_STRETCH_W; d++)
        g_col[d] = (uint8_t)(((2u * d + 1u) * NES_LCD_W) / (2u * NES_VIDEO_STRETCH_W));
    for (uint32_t d = 0; d < NES_VIDEO_STRETCH_H; d++)
        g_row[d] = (uint8_t)(((2u * d + 1u) * NES_LCD_H) / (2u * NES_VIDEO_STRETCH_H));
    g_maps_built = true;
}

} // namespace

void nes_video_fill_scanline(uint16_t *buf, uint16_t colour) {
    for (uint32_t x = 0; x < HAL_VIDEO_WIDTH; x++) buf[x] = colour;
}

void nes_video_render_scanline(uint32_t canvas_y, uint16_t *buf, const uint8_t *base,
                               const uint16_t *pal, uint8_t rotation, bool mirror,
                               bool stretch) {
    const uint8_t rot = rotation & 3u;
    if (stretch && !g_maps_built) build_maps();
    // Picture width on the canvas: 256 (292 corrected), or 240 (210
    // corrected) when rotated 90. It is 240 tall either way.
    const uint32_t w = (rot & 1u) ? (stretch ? NES_VIDEO_STRETCH_H : NES_LCD_H)
                                  : (stretch ? NES_VIDEO_STRETCH_W : NES_LCD_W);
    const uint32_t h = HAL_VIDEO_HEIGHT; // 240 either way
    const uint32_t x0 = (HAL_VIDEO_WIDTH - w) / 2u;
    if (canvas_y >= h) { nes_video_fill_scanline(buf, kBorder); return; }

    // Every pixel is written: the board hands back recycled buffers.
    for (uint32_t x = 0; x < x0; x++) buf[x] = kBorder;
    for (uint32_t x = x0 + w; x < HAL_VIDEO_WIDTH; x++) buf[x] = kBorder;

    const uint32_t v = canvas_y;
    uint16_t *out = buf + x0;
    if (stretch) {
        switch (rot) {
        case 0: {
            const uint8_t *src = row(base, v);
            if (!mirror) for (uint32_t u = 0; u < w; u++) out[u] = pal[src[g_col[u]]];
            else         for (uint32_t u = 0; u < w; u++) out[u] = pal[src[g_col[w - 1u - u]]];
            break;
        }
        case 2: {
            const uint8_t *src = row(base, NES_LCD_H - 1u - v);
            if (!mirror) for (uint32_t u = 0; u < w; u++) out[u] = pal[src[g_col[w - 1u - u]]];
            else         for (uint32_t u = 0; u < w; u++) out[u] = pal[src[g_col[u]]];
            break;
        }
        case 1: {
            const uint32_t nx = NES_LCD_W - 1u - kCrop - v;
            for (uint32_t u = 0; u < w; u++) {
                const uint32_t uu = mirror ? (w - 1u - u) : u;
                out[u] = pal[row(base, g_row[uu])[nx]];
            }
            break;
        }
        default: {
            const uint32_t nx = kCrop + v;
            for (uint32_t u = 0; u < w; u++) {
                const uint32_t uu = mirror ? (w - 1u - u) : u;
                out[u] = pal[row(base, NES_LCD_H - 1u - g_row[uu])[nx]];
            }
            break;
        }
        }
        return;
    }
    switch (rot) {
    case 0: { // upright: picture row v is NES row v
        const uint8_t *src = row(base, v);
        if (!mirror) for (uint32_t u = 0; u < w; u++) out[u] = pal[src[u]];
        else         for (uint32_t u = 0; u < w; u++) out[u] = pal[src[w - 1u - u]];
        break;
    }
    case 2: { // 180: NES row 239 - v, reversed
        const uint8_t *src = row(base, NES_LCD_H - 1u - v);
        if (!mirror) for (uint32_t u = 0; u < w; u++) out[u] = pal[src[w - 1u - u]];
        else         for (uint32_t u = 0; u < w; u++) out[u] = pal[src[u]];
        break;
    }
    case 1: { // 90 CCW: picture (u, v) is NES (x = 255 - crop - v, y = u)
        const uint32_t nx = NES_LCD_W - 1u - kCrop - v;
        for (uint32_t u = 0; u < w; u++) {
            const uint32_t uu = mirror ? (w - 1u - u) : u;
            out[u] = pal[row(base, uu)[nx]];
        }
        break;
    }
    default: { // 3, 90 CW: picture (u, v) is NES (x = crop + v, y = 239 - u)
        const uint32_t nx = kCrop + v;
        for (uint32_t u = 0; u < w; u++) {
            const uint32_t uu = mirror ? (w - 1u - u) : u;
            out[u] = pal[row(base, NES_LCD_H - 1u - uu)[nx]];
        }
        break;
    }
    }
}
