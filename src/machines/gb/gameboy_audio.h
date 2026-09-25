// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Game Boy audio: minigb_apu output -> sample ring -> the board's audio ISR.
//
// Generation happens on Core 0, once per Game Boy frame; the ISR only
// copies out of the ring. That split is the one Burger Time settled on
// (btime_audio.cpp, DEVNOTES #48/#65): nothing slow may run in an interrupt
// that shares its timing budget with the DVI scanline queue.
#ifndef GAMEBOY_AUDIO_H
#define GAMEBOY_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The rate the board plays at, the same as the arcade machines.
#define GAMEBOY_AUDIO_SAMPLE_RATE 22050

// Calls hal_audio_init(), prefills the ring with silence and registers the
// fill callback. Call before the video pump starts.
void gameboy_audio_init(void);

// Generates one Game Boy frame of audio and queues it, correcting the ring
// level by at most one sample (see gameboy_audio.cpp).
void gameboy_audio_frame(void);

// Heartbeat counters since the previous call, then reset.
typedef struct {
    uint32_t underruns;   // samples the ISR wanted and the ring didn't have
    uint32_t overruns;    // samples dropped because the ring was full
    uint32_t min_depth;   // lowest ring level the ISR saw at a call's start
    uint32_t depth;       // ring level now
    uint32_t gen_us_max;  // worst single frame's generation cost
    uint32_t produced;    // samples queued (after correction)
    uint32_t consumed;    // samples the ISR took (the board's real rate)
    uint32_t dropped;     // correction: samples removed
    uint32_t repeated;    // correction: samples added
} gameboy_audio_stats_t;
void gameboy_audio_take_stats(gameboy_audio_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
