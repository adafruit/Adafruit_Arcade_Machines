/* fruitjam-scumm - SCUMM v3/v4 on the Adafruit Fruit Jam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * State shared by the backend files (osystem.cpp, fj_core.cpp). One
 * instance, allocated by fj_core_init.
 */

#ifndef FJ_BACKEND_H
#define FJ_BACKEND_H

#include "../common/scummsys.h"
#include "../common/system.h"
#include "../common/events.h"
#include "../common/savefile.h"
#include "../common/timer.h"
#include "../graphics/palette.h"
#include "../graphics/surface.h"
#include "../backends/audiocd/audiocd.h"

#include "fj_core.h"

namespace Audio {
class MixerImpl;
}

enum FjIoOp { FJ_IO_OPEN, FJ_IO_READ, FJ_IO_WRITE, FJ_IO_SEEK, FJ_IO_SIZE, FJ_IO_CLOSE };

struct FjIoReq {
	int op;
	int h;
	const char *name;
	void *buf;
	const void *cbuf;
	int len;
	int32 pos;
	int32 result;
};

enum FjYield { FJ_YIELD_NONE, FJ_YIELD_FRAME, FJ_YIELD_IO, FJ_YIELD_DEAD };

struct FjConfig {
	char key[16];
	char value[32];
};

struct FjBackend {
	const fj_io *io;

	// coroutine handoff
	int yield;
	FjIoReq *req;
	bool started, dead;
	char error[200];
	char game[48];

	// virtual clock, advanced 1/60 s per frame
	uint64 clockUs;
	uint32 frames;

	// SCUMM's screen: 8 bits per pixel, or RGB565 for Loom PC-Engine
	byte *screen;
	int screenBpp;
	byte palette[768];
	uint16 lut16[256];
	uint8 lut8[256];
	byte dirty[FJ_SCREEN_H];
	int shakeY;
	Graphics::Surface lockSurface;

	// cursor
	byte *cursor;
	int cursorW, cursorH, cursorHX, cursorHY;
	uint32 cursorKey;
	bool cursorVisible;
	int lastCurX, lastCurY, lastCurW, lastCurH;

	// framebuffer
	void *fb;
	int fbW, fbH, bpp, x0, y0;

	// input
	int mouseX, mouseY, buttons;
	int sentX, sentY, sentButtons;
	int keyQueue[8][2];
	int keyHead, keyTail;
	int keyUp, keyUpAscii;
	int saveSlot, loadSlot;

	// audio
	int16 *ring;
	int ringFrames, ringPos;
	uint32 samples;
	int16 *scratch;
	Audio::MixerImpl *mixer;

	// log lines written on the coroutine, flushed by the host
	char log[1024];
	int logLen;

	FjConfig config[16];
	int numConfig;

	// managers handed to the engine through OSystem
	PaletteManager paletteManager;
	Common::EventManager eventManager;
	Common::SaveFileManager savefileManager;
	Common::TimerManager timerManager;
	AudioCDManager audiocdManager;
	OSystem system;
};

extern FjBackend *fj;

int32 fjIo(FjIoReq &r);
void fjLog(const char *msg);
void fjLogFlush();
void fjYieldFrame();
void fjMarkAllDirty();

#endif
