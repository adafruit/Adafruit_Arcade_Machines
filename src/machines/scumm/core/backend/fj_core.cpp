#include "fj_arduino.h" // Adafruit Arcade Machines build mode
/* fruitjam-scumm - SCUMM v3/v4 on the Adafruit Fruit Jam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Host side of the backend: fj_core_* (see fj_core.h). Runs the engine
 * coroutine one frame at a time, serves its file requests, draws the 8-bit
 * screen and cursor into the framebuffer and mixes audio into the ring.
 */

#include <setjmp.h>

#include "../common/config-manager.h"
#include "../common/textconsole.h"
#include "../audio/mixer_intern.h"

#include "../scumm/scumm.h"
#include "../scumm/scumm_v3.h"
#include "../scumm/fj_detect.h"

#include "fj_backend.h"
#include "fj_coro.h"
#include "fj_alloc.h"

int32 fjIoServe(FjIoReq &r);

static Scumm::DetectorResult *fjDetected;
static Scumm::ScummEngine *fjEngine;
static jmp_buf fjHostJmp;
static bool fjHostJmpSet;

enum { kStackSize = 48 * 1024 };
// Adafruit Arcade Machines: where the engine's coroutine stack and its
// 8-bit screen come from. Upstream takes both from the arena (PSRAM on the
// board); a port can put them in faster memory (see fj_arduino.h). The
// screen's hooks are also used by osystem.cpp's initSize().
#ifndef FJ_STACK_ALLOC
#define FJ_STACK_ALLOC(n) malloc(n)
#endif
#ifndef FJ_SCREEN_ALLOC
#define FJ_SCREEN_ALLOC(n) calloc((n), 1)
#define FJ_SCREEN_FREE(p) free(p)
#endif

#pragma mark --- engine coroutine ---

static void engineMain() {
	const Scumm::DetectorResult &dr = *fjDetected;
	switch (dr.game.version) {
	case 3:
		if (dr.game.features & Scumm::GF_OLD256)
			fjEngine = new Scumm::ScummEngine_v3(&fj->system, dr);
		else
			fjEngine = new Scumm::ScummEngine_v3old(&fj->system, dr);
		break;
	default:
		fjEngine = new Scumm::ScummEngine_v4(&fj->system, dr);
		break;
	}
	Common::Error err = fjEngine->run();
	if (err.getCode() != Common::kNoError)
		Common::strlcpy(fj->error, "engine init failed", sizeof(fj->error));
	else
		Common::strlcpy(fj->error, "game quit", sizeof(fj->error));
	fj->dead = true;
	for (;;) {
		fj->yield = FJ_YIELD_DEAD;
		fj_coro_yield();
	}
}

// Resume the engine until it ends its frame, serving file requests on the
// way.
static void runEngine() {
	for (;;) {
		fj->yield = FJ_YIELD_NONE;
		fj_coro_resume();
		fjLogFlush();
		if (fj->yield == FJ_YIELD_IO) {
			fj->req->result = fjIoServe(*fj->req);
			continue;
		}
		break;
	}
}

#pragma mark --- input ---

static void pushMouse(Common::EventType type) {
	Common::Event ev;
	ev.type = type;
	ev.mouse.x = fj->mouseX;
	ev.mouse.y = fj->mouseY;
	fj->eventManager.pushEvent(ev);
}

static void pushKey(Common::EventType type, int keycode, int ascii) {
	Common::Event ev;
	ev.type = type;
	ev.kbd.keycode = (Common::KeyCode)keycode;
	ev.kbd.ascii = ascii;
	ev.kbd.flags = 0;
	fj->eventManager.pushEvent(ev);
}

static void feedInput() {
	if (fj->mouseX != fj->sentX || fj->mouseY != fj->sentY) {
		pushMouse(Common::EVENT_MOUSEMOVE);
		fj->sentX = fj->mouseX;
		fj->sentY = fj->mouseY;
	}
	int changed = fj->buttons ^ fj->sentButtons;
	if (changed & FJ_BTN_LEFT)
		pushMouse((fj->buttons & FJ_BTN_LEFT) ? Common::EVENT_LBUTTONDOWN : Common::EVENT_LBUTTONUP);
	if (changed & FJ_BTN_RIGHT)
		pushMouse((fj->buttons & FJ_BTN_RIGHT) ? Common::EVENT_RBUTTONDOWN : Common::EVENT_RBUTTONUP);
	fj->sentButtons = fj->buttons;

	if (fj->keyUp) {
		pushKey(Common::EVENT_KEYUP, fj->keyUp, fj->keyUpAscii);
		fj->keyUp = 0;
	} else if (fj->keyHead != fj->keyTail) {
		int *k = fj->keyQueue[fj->keyHead];
		fj->keyHead = (fj->keyHead + 1) % 8;
		pushKey(Common::EVENT_KEYDOWN, k[0], k[1]);
		fj->keyUp = k[0];
		fj->keyUpAscii = k[1];
	}

	if (fjEngine && fj->saveSlot) {
		fjEngine->requestSave(fj->saveSlot, "Fruit Jam");
		fj->saveSlot = 0;
	}
	if (fjEngine && fj->loadSlot) {
		fjEngine->requestLoad(fj->loadSlot);
		fj->loadSlot = 0;
	}
}

#pragma mark --- drawing ---

static void markRows(int top, int h) {
	for (int y = MAX(top, 0); y < MIN(top + h, FJ_SCREEN_H); y++)
		fj->dirty[y] = 1;
}

// Loom PC-Engine draws in RGB565.
static inline uint8 rgb565to332(uint16 c) {
	return ((c >> 8) & 0xE0) | ((c >> 6) & 0x1C) | ((c >> 3) & 0x03);
}

static void renderRow16(int y, const uint16 *src) {
	if (fj->bpp == 16) {
		memcpy((uint16 *)fj->fb + (fj->y0 + y) * fj->fbW + fj->x0, src, FJ_SCREEN_W * 2);
	} else {
		uint8 *dst = (uint8 *)fj->fb + (fj->y0 + y) * fj->fbW + fj->x0;
		for (int x = 0; x < FJ_SCREEN_W; x++)
			dst[x] = rgb565to332(src[x]);
	}
}

static void render() {
	if (!fj->fb)
		return;

	int cx = fj->mouseX - fj->cursorHX, cy = fj->mouseY - fj->cursorHY;
	bool showCursor = fj->cursorVisible && fj->cursor && fj->cursorW;
	if (fj->lastCurW)
		markRows(fj->lastCurY, fj->lastCurH);
	if (showCursor)
		markRows(cy, fj->cursorH);

	for (int y = 0; y < FJ_SCREEN_H; y++) {
		if (!fj->dirty[y])
			continue;
		fj->dirty[y] = 0;
		int sy = y - fj->shakeY;
		const byte *src = (sy >= 0 && sy < FJ_SCREEN_H) ? fj->screen + sy * FJ_SCREEN_W * fj->screenBpp : 0;
		if (src && fj->screenBpp == 2) {
			renderRow16(y, (const uint16 *)src);
		} else if (fj->bpp == 16) {
			uint16 *dst = (uint16 *)fj->fb + (fj->y0 + y) * fj->fbW + fj->x0;
			if (src) {
				for (int x = 0; x < FJ_SCREEN_W; x++)
					dst[x] = fj->lut16[src[x]];
			} else {
				memset(dst, 0, FJ_SCREEN_W * 2);
			}
		} else {
			uint8 *dst = (uint8 *)fj->fb + (fj->y0 + y) * fj->fbW + fj->x0;
			if (src) {
				for (int x = 0; x < FJ_SCREEN_W; x++)
					dst[x] = fj->lut8[src[x]];
			} else {
				memset(dst, 0, FJ_SCREEN_W);
			}
		}
	}

	fj->lastCurW = 0;
	if (!showCursor)
		return;
	for (int j = 0; j < fj->cursorH; j++) {
		int y = cy + j;
		if (y < 0 || y >= FJ_SCREEN_H)
			continue;
		const byte *src = fj->cursor + j * fj->cursorW * fj->screenBpp;
		for (int i = 0; i < fj->cursorW; i++) {
			int x = cx + i;
			if (x < 0 || x >= FJ_SCREEN_W)
				continue;
			if (fj->screenBpp == 2) {
				uint16 c = ((const uint16 *)src)[i];
				if (c == fj->cursorKey)
					continue;
				if (fj->bpp == 16)
					((uint16 *)fj->fb)[(fj->y0 + y) * fj->fbW + fj->x0 + x] = c;
				else
					((uint8 *)fj->fb)[(fj->y0 + y) * fj->fbW + fj->x0 + x] = rgb565to332(c);
				continue;
			}
			if (src[i] == fj->cursorKey)
				continue;
			if (fj->bpp == 16)
				((uint16 *)fj->fb)[(fj->y0 + y) * fj->fbW + fj->x0 + x] = fj->lut16[src[i]];
			else
				((uint8 *)fj->fb)[(fj->y0 + y) * fj->fbW + fj->x0 + x] = fj->lut8[src[i]];
		}
	}
	fj->lastCurX = cx;
	fj->lastCurY = cy;
	fj->lastCurW = fj->cursorW;
	fj->lastCurH = fj->cursorH;
}

#pragma mark --- audio ---

// Adafruit Arcade Machines: samples mixed on top of the frame clock by
// fj_core_mix_extra(), to catch the output up after a slow frame.
static uint32 fjExtraSamples;

static void mixAudio() {
	uint32 target = (uint32)((uint64)fj->frames * FJ_AUDIO_RATE / FJ_FPS) + fjExtraSamples;
	int n = target - fj->samples;
	fj->samples = target;
	while (n > 0) {
		int chunk;
		int16 *dst;
		if (fj->ring) {
			chunk = MIN(n, fj->ringFrames - fj->ringPos);
			dst = fj->ring + fj->ringPos * 2;
		} else {
			chunk = MIN(n, 512);
			dst = fj->scratch;
		}
		fj->mixer->mixCallback((byte *)dst, chunk * 4);
		n -= chunk;
		if (fj->ring)
			fj->ringPos = (fj->ringPos + chunk) % fj->ringFrames;
	}
}

#pragma mark --- C interface ---

#ifdef FJ_NATMOD
// C++ constructors, bracketed by tools/natmod.ld. They run once the arena
// exists, since some of them allocate.
extern "C" {
typedef void (*FjCtor)(void);
extern FjCtor fj_init_array_start[], fj_init_array_end[];
}

static void runConstructors() {
	for (FjCtor *c = fj_init_array_start; c < fj_init_array_end; c++)
		(*c)();
}
#endif

extern "C" {

int fj_core_init(uint8_t *arena, size_t arena_size, const fj_io *io) {
	fj_alloc_init(arena, arena_size);
#ifdef FJ_NATMOD
	runConstructors();
#endif
	fjEngine = 0;
	fjHostJmpSet = false;
	fj = new FjBackend();
	if (!fj)
		return FJ_ERR_NOMEM;
	fj->io = io;
	fj->screen = (byte *)FJ_SCREEN_ALLOC(FJ_SCREEN_W * FJ_SCREEN_H);
	fj->screenBpp = 1;
	fj->scratch = (int16 *)malloc(512 * 4);
	void *stack = FJ_STACK_ALLOC(kStackSize);
	if (!fj->screen || !fj->scratch || !stack)
		return FJ_ERR_NOMEM;

	OSystem &sys = fj->system;
	sys._paletteManager = &fj->paletteManager;
	sys._eventManager = &fj->eventManager;
	sys._savefileManager = &fj->savefileManager;
	sys._timerManager = &fj->timerManager;
	sys._audiocdManager = &fj->audiocdManager;
	fj->mixer = new Audio::MixerImpl(FJ_AUDIO_RATE);
	fj->mixer->setReady(true);
	sys._mixer = fj->mixer;
	g_system = &sys;

	fj_core_config("subtitles", "true");
	fj_core_config("talkspeed", "60");
	fj_core_config("music_volume", "192");
	fj_core_config("sfx_volume", "192");
	fj_core_config("speech_volume", "192");
	fj_core_config("music", "pcspk");

	fjDetected = new Scumm::DetectorResult();
	const char *why = Scumm::fjDetectGame(*fjDetected);
	if (why) {
		Common::strlcpy(fj->error, why, sizeof(fj->error));
		return FJ_ERR_NOGAME;
	}
	const Scumm::DetectorResult &dr = *fjDetected;
	snprintf(fj->game, sizeof(fj->game), "%s (%s) v%d", dr.game.gameid, dr.extra ? dr.extra : "", dr.game.version);
	fj_core_config("gameid", dr.game.gameid);

	fj_coro_create(stack, kStackSize, engineMain);
	return FJ_OK;
}

const char *fj_core_error(void) {
	return fj ? fj->error : "not initialised";
}

const char *fj_core_game(void) {
	return fj ? fj->game : "";
}

void fj_core_config(const char *key, const char *value) {
	int i;
	for (i = 0; i < fj->numConfig; i++) {
		if (!strcmp(fj->config[i].key, key))
			break;
	}
	if (i == fj->numConfig) {
		if (fj->numConfig == ARRAYSIZE(fj->config))
			return;
		fj->numConfig++;
		Common::strlcpy(fj->config[i].key, key, sizeof(fj->config[i].key));
	}
	Common::strlcpy(fj->config[i].value, value, sizeof(fj->config[i].value));
}

void fj_core_set_output(void *fb, int fb_w, int fb_h, int bpp, int x0, int y0) {
	fj->fb = fb;
	fj->fbW = fb_w;
	fj->fbH = fb_h;
	fj->bpp = bpp;
	fj->x0 = x0;
	fj->y0 = y0;
	fjMarkAllDirty();
}

void fj_core_set_audio(int16_t *ring, int ring_frames, int start) {
	fj->ring = ring;
	fj->ringFrames = ring_frames;
	fj->ringPos = ring_frames ? start % ring_frames : 0;
}

uint32_t fj_core_samples(void) {
	return fj->samples;
}

void fj_core_input(int x, int y, int buttons) {
	fj->mouseX = CLIP(x, 0, FJ_SCREEN_W - 1);
	fj->mouseY = CLIP(y, 0, FJ_SCREEN_H - 1);
	fj->buttons = buttons;
}

void fj_core_key(int keycode, int ascii) {
	int next = (fj->keyTail + 1) % 8;
	if (next == fj->keyHead)
		return;
	fj->keyQueue[fj->keyTail][0] = keycode;
	fj->keyQueue[fj->keyTail][1] = ascii;
	fj->keyTail = next;
}

void fj_core_save(int slot) {
	fj->saveSlot = slot;
}

void fj_core_load(int slot) {
	fj->loadSlot = slot;
}

int fj_core_frame(void) {
	if (fj->dead)
		return 0;
	if (setjmp(fjHostJmp)) {
		fjHostJmpSet = false;
		return 0;
	}
	fjHostJmpSet = true;

	fj->frames++;
	uint64 now = (uint64)fj->frames * 1000000 / FJ_FPS;
	uint32 elapsedMs = (uint32)(now / 1000 - fj->clockUs / 1000);
	fj->clockUs = now;
	fj->timerManager.advance(elapsedMs);

	feedInput();
	runEngine();
	render();
	mixAudio();

	fjHostJmpSet = false;
	return fj->dead ? 0 : 1;
}

// Adafruit Arcade Machines: see fj_core.h.
void fj_core_mix_extra(int n) {
	if (!fj || fj->dead || n <= 0)
		return;
	if (setjmp(fjHostJmp)) {
		fjHostJmpSet = false;
		return;
	}
	fjHostJmpSet = true;
	fjExtraSamples += (uint32)n;
	mixAudio();
	fjHostJmpSet = false;
}

size_t fj_core_mem_used(void) {
	return fj_alloc_used();
}

size_t fj_core_mem_peak(void) {
	return fj_alloc_peak();
}

size_t fj_core_stack_used(void) {
	return fj_coro_stack_used();
}

size_t fj_core_stack_size(void) {
	return kStackSize;
}

} // extern "C"

// Called by error() when it runs on the host stack.
void fjHostAbort() {
	if (fjHostJmpSet)
		longjmp(fjHostJmp, 1);
	for (;;) {
	}
}
