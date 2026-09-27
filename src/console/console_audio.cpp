// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See console_audio.h.
//
// Generation happens on core 0, once per emulated frame; the ISR only copies
// out of the ring. That split is the one Burger Time settled on
// (btime_audio.cpp, DEVNOTES #48/#65): nothing slow may run in an interrupt
// that shares its timing budget with the DVI scanline queue.
//
// The ring is Burger Time's shape: 2048 deep, a target three times the
// ISR's 256-sample drain, and a deadband so the level isn't nudged every
// frame.
//
// THE LEVEL CORRECTION drops samples from the end of a frame when the ring
// is above target and repeats the last one when it is below -- at most
// MAX_CORRECTION a frame, so never a burst: Burger Time's first, burstier
// correction starved the video queue (DEVNOTES #65).
//
// Measured on the Fruit Jam (the heartbeat's prod/cons/drop/rep counters):
// the ISR consumes exactly 22050/s -- 86 or 87 drains of 256 per second.
// The Game Boy's minigb makes 368 samples a frame at 22000 Hz, 22080/s, so
// ~30 a second are dropped; the NES's nofrendo makes 367 at 22050, 22020/s,
// so ~30 a second are repeated. Either way the level settles just outside
// the deadband's edge; that is where a threshold controller holds a
// constant surplus or deficit, not a fault.
//
// UP TO THREE PER FRAME, stepped by how far the level is off, is headroom,
// not a fix. It was added when the Game Boy ring's resting level was
// misread as the correction being saturated -- inferred as a slow 22020 Hz
// audio clock, which the counters then disproved. At the resting level it
// only ever moves one sample at a time; the extra room is for a real drift.
#include "console/console_audio.h"

#include <string.h>

#include "arch/arch.h"
#include "hal/arcade_hal_audio.h"

// 4096 deep, so a two-core board's larger target (console_audio_set_target)
// still leaves room for a burst above it.
#define RING_SIZE      4096u // power of two
#define RING_TARGET_DEFAULT 768u
#define RING_DEADBAND  96u
#define MAX_CORRECTION 3u

static int16_t g_ring[RING_SIZE];
static uint32_t g_target = RING_TARGET_DEFAULT;
static uint32_t g_volume = CONSOLE_AUDIO_VOLUME_FULL;
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
    g_target = RING_TARGET_DEFAULT;
    g_volume = CONSOLE_AUDIO_VOLUME_FULL;
    g_head = g_target; // prefilled with silence, before the pump starts
    g_underruns = g_overruns = 0;
    g_min_depth = 0xFFFFFFFFu;
    hal_audio_set_fill_callback(&fill_audio);
}

void console_audio_set_target(uint32_t samples) {
    if (samples > RING_SIZE / 2u) samples = RING_SIZE / 2u;
    // Raise the prefill to match, before the pump has drained anything.
    const uint32_t saved = hal_audio_enter_critical();
    if (g_head - g_tail < samples) g_head = g_tail + samples;
    hal_audio_exit_critical(saved);
    g_target = samples;
}

void console_audio_set_volume(uint32_t volume) {
    g_volume = volume > CONSOLE_AUDIO_VOLUME_FULL ? CONSOLE_AUDIO_VOLUME_FULL : volume;
}

// 3 dB apart (x1.41), the steps the Feather's defaults were chosen from.
static const uint16_t kVolumeSteps[] = {
    2, 3, 4, 6, 8, 11, 16, 23, 32, 45, 64, 90, 128, 181, 256,
};
#define VOLUME_STEP_COUNT (sizeof kVolumeSteps / sizeof kVolumeSteps[0])

uint32_t console_audio_volume_step(int steps) {
    // From the step nearest the current volume, which need not be on the
    // table (the NES Feather's default is 48).
    uint32_t idx = 0, best = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < VOLUME_STEP_COUNT; i++) {
        const uint32_t d = kVolumeSteps[i] > g_volume ? kVolumeSteps[i] - g_volume
                                                      : g_volume - kVolumeSteps[i];
        if (d < best) { best = d; idx = i; }
    }
    int32_t to = (int32_t)idx + steps;
    if (to < 0) to = 0;
    if (to >= (int32_t)VOLUME_STEP_COUNT) to = VOLUME_STEP_COUNT - 1;
    g_volume = kVolumeSteps[to];
    return g_volume;
}

uint32_t console_audio_volume(void) { return g_volume; }

void console_audio_push(const int16_t *samples, uint32_t n) {
    static int16_t frame[CONSOLE_AUDIO_MAX_FRAME + MAX_CORRECTION];
    if (n > CONSOLE_AUDIO_MAX_FRAME) n = CONSOLE_AUDIO_MAX_FRAME;
    // The volume is applied here, once per frame on the emulation core, and
    // never in the ISR, which only copies. Full volume is a plain copy.
    const uint32_t vol = g_volume;
    if (vol >= CONSOLE_AUDIO_VOLUME_FULL) {
        memcpy(frame, samples, n * sizeof(int16_t));
    } else {
        for (uint32_t i = 0; i < n; i++)
            frame[i] = (int16_t)(((int32_t)samples[i] * (int32_t)vol) / (int32_t)CONSOLE_AUDIO_VOLUME_FULL);
    }

    const uint32_t depth = g_head - g_tail;
    if (depth > g_target + RING_DEADBAND) {
        uint32_t k = (depth - g_target) / RING_DEADBAND;
        if (k > MAX_CORRECTION) k = MAX_CORRECTION;
        g_dropped += (n > k) ? k : n;
        n = (n > k) ? n - k : 0;
    } else if (depth + RING_DEADBAND < g_target && n > 0) {
        uint32_t k = (g_target - depth) / RING_DEADBAND;
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
