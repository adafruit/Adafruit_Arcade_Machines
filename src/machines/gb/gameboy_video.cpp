// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See gameboy_video.h.
#include "gameboy_video.h"

#include "arch/arch.h"
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

// --- Bigger pictures on the canvas (gameboy_video.h) ----------------------

namespace {

gameboy_scale_t g_scale = GAMEBOY_SCALE_1X;

const uint32_t kCanvasW = 320u, kCanvasH = 240u;   // the HAL's canvas

// Picture row v (0..pic_h-1) as colours, pic_w of them, after rotation and
// mirror: the same mapping as the 1x renderer's.
void fetch_row(const gameboy_row_t *fb, uint8_t rot, bool mirror, uint32_t v,
               uint16_t *out) {
    const uint32_t n = pic_w(rot);
    if (rot == 0u || rot == 2u) {
        const uint8_t *src = fb[rot == 0u ? v : GAMEBOY_LCD_H - 1u - v];
        const bool reverse = (rot == 2u) != mirror;
        for (uint32_t u = 0; u < n; u++) out[u] = g_colour[src[reverse ? n - 1u - u : u]];
    } else {
        // 90 CCW: picture (u, v) is Game Boy (x = 159 - v, y = u);
        // 90 CW:  picture (u, v) is Game Boy (x = v, y = 143 - u).
        const uint32_t gx = (rot == 1u) ? GAMEBOY_LCD_W - 1u - v : v;
        for (uint32_t u = 0; u < n; u++) {
            const uint32_t uu = mirror ? n - 1u - u : u;
            const uint32_t gy = (rot == 1u) ? uu : GAMEBOY_LCD_H - 1u - uu;
            out[u] = g_colour[fb[gy][gx]];
        }
    }
}

// RGB565 blend, a*w + b*(32-w), all three channels at once: spread the
// channels apart in 32 bits so the products do not overlap.
inline uint32_t spread(uint16_t c) { return ((uint32_t)c | ((uint32_t)c << 16)) & 0x07E0F81Fu; }
inline uint16_t blend(uint16_t a, uint16_t b, uint32_t w) {
    if (w >= 32u) return a;
    const uint32_t x = ((spread(a) * w + spread(b) * (32u - w)) >> 5) & 0x07E0F81Fu;
    return (uint16_t)(x | (x >> 16));
}

// FIT. Upscaling by s = 240 / picture height, each canvas pixel covers
// 1/s < 1 source pixels, so at most two: source i with weight w/32 and
// i + 1 with the rest. Per canvas column and row, rebuilt when the mode or
// rotation changes -- at most once a paint, since both change between
// paints.
struct tap_t { uint8_t i; uint8_t w; };
tap_t    g_cols[kCanvasW], g_rows[kCanvasH];
uint32_t g_fit_w = 0, g_fit_x0 = 0;
int      g_fit_key = -1;

// `ph` is the picture's height: canvas pixel j covers source
// [j * ph/240, (j + 1) * ph/240), in 1/256ths of a pixel -- the SAME ratio
// on both axes, so the picture keeps its shape; `in_n` only bounds it.
void build_taps(tap_t *t, uint32_t out_n, uint32_t in_n, uint32_t ph, bool smooth) {
    for (uint32_t j = 0; j < out_n; j++) {
        const uint32_t a = j * ph * 256u / kCanvasH;           // start
        const uint32_t b = (j + 1u) * ph * 256u / kCanvasH;    // end
        uint32_t i = a >> 8;
        uint32_t w = 32u;
        if (smooth) {
            const uint32_t edge = (i + 1u) << 8;
            if (b > edge && i + 1u < in_n) w = (edge - a) * 32u / (b - a);
        } else {
            i = ((a + b) / 2u) >> 8;                           // the centre's pixel
        }
        if (i >= in_n) i = in_n - 1u;
        t[j].i = (uint8_t)i;
        t[j].w = (uint8_t)w;
    }
}

void prepare_fit(uint8_t rot) {
    const int key = (int)g_scale * 4 + rot;
    if (key == g_fit_key) return;
    g_fit_key = key;
    const bool smooth = (g_scale == GAMEBOY_SCALE_FIT_SMOOTH);
    const uint32_t pw = pic_w(rot), ph = pic_h(rot);
    g_fit_w = pw * kCanvasH / ph;                              // 266 or 216
    g_fit_x0 = (kCanvasW - g_fit_w) / 2u;
    build_taps(g_cols, g_fit_w, pw, ph, smooth);
    build_taps(g_rows, kCanvasH, ph, ph, smooth);
}

void scale_row(const uint16_t *src, uint16_t *out) {
    for (uint32_t x = 0; x < g_fit_w; x++) {
        const tap_t t = g_cols[x];
        out[x] = (t.w >= 32u) ? src[t.i] : blend(src[t.i], src[t.i + 1u], t.w);
    }
}

void render_scanline_fit(uint32_t canvas_y, uint16_t *buf,
                         const gameboy_row_t *fb, uint8_t rot, bool mirror) {
    prepare_fit(rot);
    for (uint32_t x = 0; x < g_fit_x0; x++) buf[x] = kBorder;
    for (uint32_t x = g_fit_x0 + g_fit_w; x < kCanvasW; x++) buf[x] = kBorder;
    uint16_t row[GAMEBOY_LCD_W];
    uint16_t *out = buf + g_fit_x0;
    const tap_t r = g_rows[canvas_y];
    fetch_row(fb, rot, mirror, r.i, row);
    scale_row(row, out);
    if (r.w < 32u) {
        // Between two source rows: scale the next one too and blend.
        uint16_t next[GAMEBOY_LCD_W], scaled[kCanvasW];
        fetch_row(fb, rot, mirror, r.i + 1u, next);
        scale_row(next, scaled);
        for (uint32_t x = 0; x < g_fit_w; x++) out[x] = blend(out[x], scaled[x], r.w);
    }
}

// 2X: canvas line canvas_y shows picture line canvas_y + crop, each Game
// Boy pixel written twice across.
void render_scanline_2x(uint32_t canvas_y, uint16_t *buf,
                        const gameboy_row_t *fb, uint8_t rot, bool mirror) {
    const uint32_t n = pic_w(rot), w = 2u * n, h = 2u * pic_h(rot);
    const uint32_t x0 = (kCanvasW - w) / 2u;
    const uint32_t over = h - kCanvasH;               // 48 upright, 80 rotated
    const uint32_t crop = (g_scale == GAMEBOY_SCALE_2X_TOP)    ? 0u
                        : (g_scale == GAMEBOY_SCALE_2X_BOTTOM) ? over
                        : over / 2u;
    for (uint32_t x = 0; x < x0; x++) buf[x] = kBorder;
    for (uint32_t x = x0 + w; x < kCanvasW; x++) buf[x] = kBorder;
    uint16_t row[GAMEBOY_LCD_W];
    fetch_row(fb, rot, mirror, (canvas_y + crop) / 2u, row);
    uint16_t *out = buf + x0;
    for (uint32_t u = 0; u < n; u++) { out[0] = row[u]; out[1] = row[u]; out += 2; }
}

} // namespace

void gameboy_video_set_scale(gameboy_scale_t scale) {
    g_scale = (scale < GAMEBOY_SCALE_COUNT) ? scale : GAMEBOY_SCALE_1X;
}

const char *gameboy_video_scale_name(gameboy_scale_t scale) {
    switch (scale) {
    case GAMEBOY_SCALE_FIT_NEAREST: return "fit, nearest";
    case GAMEBOY_SCALE_FIT_SMOOTH:  return "fit, smooth";
    case GAMEBOY_SCALE_2X_CENTRE:   return "2x, centred";
    case GAMEBOY_SCALE_2X_TOP:      return "2x, top kept";
    case GAMEBOY_SCALE_2X_BOTTOM:   return "2x, bottom kept";
    default:                        return "1x";
    }
}

void gameboy_video_render_scanline(uint32_t canvas_y, uint16_t *buf,
                                   const gameboy_row_t *fb,
                                   uint8_t rotation, bool mirror) {
    const uint8_t rot = rotation & 3u;
    if (g_scale == GAMEBOY_SCALE_FIT_NEAREST || g_scale == GAMEBOY_SCALE_FIT_SMOOTH) {
        render_scanline_fit(canvas_y, buf, fb, rot, mirror);
        return;
    }
    if (g_scale != GAMEBOY_SCALE_1X) {
        render_scanline_2x(canvas_y, buf, fb, rot, mirror);
        return;
    }
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

// --- 3x scan-out (gameboy_video.h) ------------------------------------------

namespace {

const uint32_t kOutW = 640u, kOutH = 480u;

// Written by the machine (core 0), read by the scan-out (the video core).
const gameboy_row_t *volatile g_pub_front = nullptr;
volatile uint8_t g_pub_rot = 0;
volatile bool    g_pub_mirror = false;

// The scan-out's own copy for the output frame in progress, latched at
// line 0 so a publish mid-frame changes nothing until the next one.
const gameboy_row_t *g_scan_front = nullptr;
uint8_t g_scan_rot = 0;
bool    g_scan_mirror = false;

} // namespace

void gameboy_video_publish(const gameboy_row_t *front, uint8_t rotation, bool mirror) {
    g_pub_rot = (uint8_t)(rotation & 3u);
    g_pub_mirror = mirror;
    g_pub_front = front;
}

// No switch (a jump table would be read from flash) and no memset: GCC
// turns a loop storing zeros into a call to it, and memset is in flash.
__attribute__((optimize("no-tree-loop-distribute-patterns")))
void ARCADE_FAST_FUNC(gameboy_video_scanout_3x)(uint32_t line, uint32_t *dst) {
    if (line == 0) {
        g_scan_front = g_pub_front;
        g_scan_rot = g_pub_rot;
        g_scan_mirror = g_pub_mirror;
    }
    const gameboy_row_t *fb = g_scan_front;
    const uint8_t rot = g_scan_rot;
    const bool mirror = g_scan_mirror;
    const uint32_t w = pic_w(rot) * 3u, h = pic_h(rot) * 3u;
    const uint32_t x0 = (kOutW - w) / 2u, y0 = (kOutH - h) / 2u;

    if (!fb || line < y0 || line >= y0 + h) {
        for (uint32_t i = 0; i < kOutW / 2u; i++) dst[i] = 0u;
        return;
    }
    // x0 is 80 or 104, even either way, so the borders are whole words.
    for (uint32_t i = 0; i < x0 / 2u; i++) dst[i] = 0u;
    for (uint32_t i = (x0 + w) / 2u; i < kOutW / 2u; i++) dst[i] = 0u;

    uint16_t *out = (uint16_t *)dst + x0;
    const uint32_t v = (line - y0) / 3u;     // the row within the picture
    const uint32_t n = w / 3u;               // Game Boy pixels across it
    if (rot == 0u || rot == 2u) {
        const uint8_t *src = fb[rot == 0u ? v : GAMEBOY_LCD_H - 1u - v];
        const bool reverse = (rot == 2u) != mirror;
        for (uint32_t u = 0; u < n; u++) {
            const uint16_t c = g_colour[src[reverse ? n - 1u - u : u]];
            out[0] = c; out[1] = c; out[2] = c;
            out += 3;
        }
    } else {
        // 90 CCW: picture (u, v) is Game Boy (x = 159 - v, y = u);
        // 90 CW:  picture (u, v) is Game Boy (x = v, y = 143 - u).
        const uint32_t gx = (rot == 1u) ? GAMEBOY_LCD_W - 1u - v : v;
        for (uint32_t u = 0; u < n; u++) {
            const uint32_t uu = mirror ? n - 1u - u : u;
            const uint32_t gy = (rot == 1u) ? uu : GAMEBOY_LCD_H - 1u - uu;
            const uint16_t c = g_colour[fb[gy][gx]];
            out[0] = c; out[1] = c; out[2] = c;
            out += 3;
        }
    }
}
