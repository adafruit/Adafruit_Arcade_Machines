/* fruitjam-scumm - SCUMM v3/v4 on the Adafruit Fruit Jam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The C interface between the SCUMM engine and whatever drives it: the
 * MicroPython glue (src/modscumm.c) on the board, tools/host_main.cpp on a
 * computer. Everything here is called on the host's own stack. The engine
 * runs on a coroutine of its own and only ever reaches the host through
 * fj_core_frame() returning.
 */

#ifndef FJ_CORE_H
#define FJ_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* File access, supplied by the host, always called on the host stack.
 * Names are plain file names inside the game folder; the host does the
 * case-insensitive lookup. Handles are small non-negative ints. */
typedef struct fj_io {
	int (*open)(void *ctx, const char *name, int write); /* -1 if missing */
	int (*read)(void *ctx, int h, void *buf, int len);   /* bytes read */
	int (*write)(void *ctx, int h, const void *buf, int len);
	int (*seek)(void *ctx, int h, int32_t pos);           /* absolute */
	int32_t (*size)(void *ctx, int h);
	void (*close)(void *ctx, int h);
	void (*log)(void *ctx, const char *msg);
	void *ctx;
} fj_io;

/* Display: SCUMM's 320x200 8-bit screen, drawn into the host framebuffer. */
#define FJ_SCREEN_W 320
#define FJ_SCREEN_H 200
#define FJ_FPS 60

/* Mouse buttons for fj_core_input. */
#define FJ_BTN_LEFT 1
#define FJ_BTN_RIGHT 2

/* Keys for fj_core_key (a subset of ScummVM's keycodes, ASCII where it has
 * one). */
#define FJ_KEY_ESCAPE 27
#define FJ_KEY_RETURN 13
#define FJ_KEY_SPACE 32
#define FJ_KEY_F1 282 /* F1..F9 = 282..290, as in ScummVM */

/* Error codes from fj_core_init. */
#define FJ_OK 0
#define FJ_ERR_NOGAME 1   /* no SCUMM game recognised in the folder */
#define FJ_ERR_NOMEM 2    /* arena too small */

/* Bring the core up on `arena`. On the board every allocation the engine
 * makes comes out of the arena; on a computer the arena only holds the
 * coroutine stack and screen. Detects the game; the engine itself starts on
 * the first fj_core_frame(). Returns FJ_OK or an FJ_ERR_*. */
int fj_core_init(uint8_t *arena, size_t arena_size, const fj_io *io);

/* Human readable reason for the last error or stop, never NULL. */
const char *fj_core_error(void);

/* Game description, e.g. "loom (Steam)", after fj_core_init. */
const char *fj_core_game(void);

/* Settings read by the engine through ConfMan ("music", "subtitles",
 * "talkspeed", ...). Call before the first frame. */
void fj_core_config(const char *key, const char *value);

/* Framebuffer: width fb_w pixels, bpp 16 (RGB565) or 8 (RGB332), the game
 * picture's top-left corner at (x0, y0). */
void fj_core_set_output(void *fb, int fb_w, int fb_h, int bpp, int x0, int y0);

/* Audio: interleaved stereo int16 ring of ring_frames frames at
 * FJ_AUDIO_RATE. Each frame appends its share of samples from `start`. */
#define FJ_AUDIO_RATE 22050
void fj_core_set_audio(int16_t *ring, int ring_frames, int start);

/* Running count of audio frames produced (one per 1/FJ_AUDIO_RATE s). */
uint32_t fj_core_samples(void);

/* Adafruit Arcade Machines: mix `n` more audio frames into the ring now,
 * beyond the 1/60 s that fj_core_frame() mixes, without advancing the
 * game. The music players are driven by the samples they make, so their
 * tempo is unchanged. For catching the output up after a slow frame (a
 * room load or a full redraw), which mixes no more than a fast one. */
void fj_core_mix_extra(int n);

/* Input for the next frame: mouse in game coordinates (0..319, 0..199) and
 * FJ_BTN_* bits. */
void fj_core_input(int x, int y, int buttons);

/* Queue a key press (down and up on consecutive frames). */
void fj_core_key(int keycode, int ascii);

/* Request a save or load of `slot` (1..99) at the next safe point. */
void fj_core_save(int slot);
void fj_core_load(int slot);

/* Run one 1/60 s frame. Returns 1 while running, 0 once the engine has
 * stopped (quit or error; see fj_core_error). */
int fj_core_frame(void);

/* Bytes of the arena in use and the high water mark. */
size_t fj_core_mem_used(void);
size_t fj_core_mem_peak(void);

/* Deepest the engine's stack has been, and its size, in bytes. */
size_t fj_core_stack_used(void);
size_t fj_core_stack_size(void);

#ifdef __cplusplus
}
#endif

#endif
