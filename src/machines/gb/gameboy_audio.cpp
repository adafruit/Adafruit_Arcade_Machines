// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See gameboy_audio.h. The ring, the ISR copy and the level correction are
// the shared console ring (console/console_audio.cpp, where their design and
// measurements are recorded); this file only feeds it a Game Boy frame at a
// time and times the generation.
#include "gameboy_audio.h"

#include "arch/arch.h"
#include "gameboy_core.h"
#include "console/console_audio.h"

static uint32_t g_gen_us_max;

void gameboy_audio_init(void) {
    console_audio_init(GAMEBOY_AUDIO_SAMPLE_RATE);
    g_gen_us_max = 0;
}

void gameboy_audio_frame(void) {
    static int16_t frame[GAMEBOY_APU_MAX_SAMPLES];
    const uint64_t t0 = ARCADE_TIME_US64();
    const uint32_t n = gameboy_core_audio_frame(frame);
    console_audio_push(frame, n);
    const uint32_t us = (uint32_t)(ARCADE_TIME_US64() - t0);
    if (us > g_gen_us_max) g_gen_us_max = us;
}

void gameboy_audio_take_stats(gameboy_audio_stats_t *out) {
    console_audio_stats_t s;
    console_audio_take_stats(&s);
    out->underruns = s.underruns;
    out->overruns = s.overruns;
    out->min_depth = s.min_depth;
    out->depth = s.depth;
    out->produced = s.produced;
    out->consumed = s.consumed;
    out->dropped = s.dropped;
    out->repeated = s.repeated;
    out->gen_us_max = g_gen_us_max;
    g_gen_us_max = 0;
}
