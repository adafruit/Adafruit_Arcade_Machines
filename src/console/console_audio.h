// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// The consoles' audio ring: a frame of samples in from the emulation loop,
// copied out by the board's audio ISR. Shared by the Game Boy and the NES;
// it began as the Game Boy's (DEVNOTES #127) and its design notes and
// measurements are in console_audio.cpp.
//
// The emulation loop pushes one frame of mono samples at a time. The ring
// holds 4096, aims for 768, and nudges the level by at most 3 samples a
// frame (dropped when high, the last repeated when low; the aim is settable
// per board, console_audio_set_target()), because the
// console's sample count per frame and the board's real rate never match
// exactly.
#ifndef CONSOLE_AUDIO_H
#define CONSOLE_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONSOLE_AUDIO_MAX_FRAME 1024 // samples per push, at most

// Starts the board's audio at `rate` and registers the ISR copy.
void console_audio_init(uint32_t rate);

// The level the ring aims for, in samples (default 768). A board whose
// emulation runs in bursts on another core needs more: on the Feather the
// emulation task makes two frames of samples at once and then waits a
// whole ~33 ms paint, and 768 left the consumer below its own 256-sample
// drain (Donkey Kong needed 1250; DEVNOTES #119). "The target is a board
// property, not a game one." Call after console_audio_init().
void console_audio_set_target(uint32_t samples);

// One frame of mono samples from the emulation loop (core 0).
void console_audio_push(const int16_t *samples, uint32_t n);

typedef struct {
    uint32_t underruns;   // samples the ISR wanted and the ring didn't have
    uint32_t overruns;    // samples dropped because the ring was full
    uint32_t min_depth;   // lowest ring level the ISR saw at a call's start
    uint32_t depth;       // ring level now
    uint32_t produced;    // samples queued (after correction)
    uint32_t consumed;    // samples the ISR took (the board's real rate)
    uint32_t dropped;     // correction: samples removed
    uint32_t repeated;    // correction: samples added
} console_audio_stats_t;
void console_audio_take_stats(console_audio_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
