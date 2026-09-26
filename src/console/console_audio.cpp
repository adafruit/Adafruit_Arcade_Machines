// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See console_audio.h. The ring, the ISR copy and the level correction are
// the Game Boy's (machines/gb/gameboy_audio.cpp), unchanged in behaviour.
#include "console/console_audio.h"

#include <string.h>

#include "arch/arch.h"
#include "hal/arcade_hal_audio.h"

#define RING_SIZE      2048u // power of two
#define RING_TARGET    768u
#define RING_DEADBAND  96u
#define MAX_CORRECTION 3u

static int16_t g_ring[RING_SIZE];
static volatile uint32_t g_head; // producer (core 0)
static volatile uint32_t g_tail; // consumer (audio ISR)

static volatile uint32_t g_underruns;
static uint32_t g_overruns;
static volatile uint32_t g_min_depth = 0xFFFFFFFFu;
static uint32_t g_produced, g_dropped, g_repeated;
static volatile uint32_t g_consumed;

// Runs in the board's audio ISR, so never from flash (an XIP miss here can
// starve the DVI queue; DEVNOTES #3/#7). It only copies.
static void ARCADE_FAST_FUNC(fill_audio)(int32_t *out, int count) {
    uint32_t tail = g_tail;
    const uint32_t head = g_head;
    { const uint32_t d = head - tail; if (d < g_min_depth) g_min_depth = d; }
    for (int i = 0; i < count; i++) {
        int16_t s = 0;
        if (tail != head) { s = g_ring[tail & (RING_SIZE - 1u)]; tail++; }
        else              { g_underruns++; }
        g_consumed++;
        out[i] = ((int32_t)s << 16) | (uint16_t)s; // mono on both channels
    }
    g_tail = tail;
}

void console_audio_init(uint32_t rate) {
    hal_audio_init(rate);
    memset(g_ring, 0, sizeof g_ring);
    g_tail = 0;
    g_head = RING_TARGET; // prefilled with silence, before the pump starts
    g_underruns = g_overruns = 0;
    g_min_depth = 0xFFFFFFFFu;
    hal_audio_set_fill_callback(&fill_audio);
}

void console_audio_push(const int16_t *samples, uint32_t n) {
    static int16_t frame[CONSOLE_AUDIO_MAX_FRAME + MAX_CORRECTION];
    if (n > CONSOLE_AUDIO_MAX_FRAME) n = CONSOLE_AUDIO_MAX_FRAME;
    memcpy(frame, samples, n * sizeof(int16_t));

    const uint32_t depth = g_head - g_tail;
    if (depth > RING_TARGET + RING_DEADBAND) {
        uint32_t k = (depth - RING_TARGET) / RING_DEADBAND;
        if (k > MAX_CORRECTION) k = MAX_CORRECTION;
        g_dropped += (n > k) ? k : n;
        n = (n > k) ? n - k : 0;
    } else if (depth + RING_DEADBAND < RING_TARGET && n > 0) {
        uint32_t k = (RING_TARGET - depth) / RING_DEADBAND;
        if (k > MAX_CORRECTION) k = MAX_CORRECTION;
        for (uint32_t i = 0; i < k; i++) { frame[n] = frame[n - 1]; n++; }
        g_repeated += k;
    }

    uint32_t head = g_head;
    for (uint32_t i = 0; i < n; i++) {
        if (head - g_tail >= RING_SIZE) { g_overruns += n - i; break; }
        g_ring[head & (RING_SIZE - 1u)] = frame[i];
        head++;
    }
    g_produced += head - g_head;
    g_head = head;
}

void console_audio_take_stats(console_audio_stats_t *out) {
    const uint32_t saved = hal_audio_enter_critical();
    out->underruns = g_underruns; g_underruns = 0;
    out->min_depth = (g_min_depth == 0xFFFFFFFFu) ? 0u : g_min_depth;
    g_min_depth = 0xFFFFFFFFu;
    out->consumed = g_consumed; g_consumed = 0;
    hal_audio_exit_critical(saved);
    out->overruns = g_overruns; g_overruns = 0;
    out->depth = g_head - g_tail;
    out->produced = g_produced; g_produced = 0;
    out->dropped = g_dropped; g_dropped = 0;
    out->repeated = g_repeated; g_repeated = 0;
}
