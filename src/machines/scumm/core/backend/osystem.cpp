#include "fj_arduino.h" // Adafruit Arcade Machines build mode
/* fruitjam-scumm - SCUMM v3/v4 on the Adafruit Fruit Jam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The OSystem side of the backend: everything the engine calls. File I/O,
 * the 8-bit screen, palette, cursor, events, the virtual clock, timers,
 * save files and the tiny ConfMan. These run on the engine coroutine; the
 * host side (frame loop, rendering, mixing) is in fj_core.cpp.
 */

#include "../common/config-manager.h"
#include "../common/debug-channels.h"
#include "../common/debug.h"
#include "../common/file.h"
#include "../common/textconsole.h"
#include "../graphics/cursorman.h"
#include "../engines/engine.h"

#include "fj_backend.h"
#include "fj_coro.h"

FjBackend *fj;
OSystem *g_system;
Engine *g_engine;

namespace Common {
ConfigManager ConfMan;
DebugManager DebugMan;
}
namespace Graphics {
CursorManager CursorMan;
}

int gDebugLevel = -1;
bool gDebugChannelsOnly = false;

#pragma mark --- I/O and logging ---

static int32 ioDispatch(FjIoReq &r) {
	const fj_io *io = fj->io;
	switch (r.op) {
	case FJ_IO_OPEN:
		return io->open(io->ctx, r.name, r.len);
	case FJ_IO_READ:
		return io->read(io->ctx, r.h, r.buf, r.len);
	case FJ_IO_WRITE:
		return io->write(io->ctx, r.h, r.cbuf, r.len);
	case FJ_IO_SEEK:
		return io->seek(io->ctx, r.h, r.pos);
	case FJ_IO_SIZE:
		return io->size(io->ctx, r.h);
	case FJ_IO_CLOSE:
		io->close(io->ctx, r.h);
		return 0;
	}
	return -1;
}

// Run an I/O request on the host stack. From the coroutine that means a
// round trip through the host's frame loop (fj_core.cpp).
int32 fjIo(FjIoReq &r) {
	if (!fj_coro_active())
		return ioDispatch(r);
	fj->req = &r;
	fj->yield = FJ_YIELD_IO;
	fj_coro_yield();
	return r.result;
}

int32 fjIoServe(FjIoReq &r) {
	return ioDispatch(r);
}

void fjLogFlush() {
	if (fj->logLen) {
		fj->log[fj->logLen] = 0;
		fj->io->log(fj->io->ctx, fj->log);
		fj->logLen = 0;
	}
}

void fjLog(const char *msg) {
	if (!fj_coro_active()) {
		fj->io->log(fj->io->ctx, msg);
		return;
	}
	int n = strlen(msg);
	if (fj->logLen + n + 2 >= (int)sizeof(fj->log))
		return; // drop it; the host flushes once per resume
	memcpy(fj->log + fj->logLen, msg, n);
	fj->logLen += n;
	fj->log[fj->logLen++] = '\n';
}

#pragma mark --- Streams over host files ---

class FjReadStream : public Common::SeekableReadStream {
	enum { kBufSize = 2048 };
	int _h;
	int32 _size, _pos;
	int32 _bufStart, _bufLen; // file offset and length of the buffered bytes
	bool _eos;
	byte _buf[kBufSize];

public:
	FjReadStream(int h, int32 size) : _h(h), _size(size), _pos(0), _bufStart(0), _bufLen(0), _eos(false) {}
	~FjReadStream() override {
		FjIoReq r = { FJ_IO_CLOSE, _h };
		fjIo(r);
	}

	bool eos() const override { return _eos; }
	void clearErr() override { _eos = false; }
	int32 pos() const override { return _pos; }
	int32 size() const override { return _size; }

	bool seek(int32 offs, int whence = SEEK_SET) override {
		if (whence == SEEK_CUR)
			offs += _pos;
		else if (whence == SEEK_END)
			offs += _size;
		if (offs < 0)
			offs = 0;
		if (offs > _size)
			offs = _size;
		_pos = offs;
		_eos = false;
		return true;
	}

	uint32 read(void *dataPtr, uint32 dataSize) override {
		byte *dst = (byte *)dataPtr;
		uint32 done = 0;
		while (done < dataSize) {
			if (_pos >= _size) {
				_eos = true;
				break;
			}
			if (_pos >= _bufStart && _pos < _bufStart + _bufLen) {
				uint32 n = MIN<uint32>(dataSize - done, _bufStart + _bufLen - _pos);
				memcpy(dst + done, _buf + (_pos - _bufStart), n);
				done += n;
				_pos += n;
				continue;
			}
			uint32 want = dataSize - done;
			FjIoReq s = { FJ_IO_SEEK, _h };
			s.pos = _pos;
			fjIo(s);
			if (want >= kBufSize) {
				// Big reads go straight into the caller's buffer.
				FjIoReq r = { FJ_IO_READ, _h };
				r.buf = dst + done;
				r.len = MIN<int32>(want, _size - _pos);
				int32 n = fjIo(r);
				if (n <= 0) {
					_eos = true;
					break;
				}
				done += n;
				_pos += n;
			} else {
				FjIoReq r = { FJ_IO_READ, _h };
				r.buf = _buf;
				r.len = MIN<int32>(kBufSize, _size - _pos);
				int32 n = fjIo(r);
				if (n <= 0) {
					_eos = true;
					break;
				}
				_bufStart = _pos;
				_bufLen = n;
			}
		}
		return done;
	}
};

class FjWriteStream : public Common::WriteStream {
	enum { kBufSize = 8192 };
	int _h;
	int32 _pos;
	int _len;
	bool _err;
	byte _buf[kBufSize];

public:
	FjWriteStream(int h) : _h(h), _pos(0), _len(0), _err(false) {}
	~FjWriteStream() override {
		flush();
		FjIoReq r = { FJ_IO_CLOSE, _h };
		fjIo(r);
	}

	bool err() const override { return _err; }
	void clearErr() override { _err = false; }
	int32 pos() const override { return _pos; }

	bool flush() override {
		if (_len) {
			FjIoReq r = { FJ_IO_WRITE, _h };
			r.cbuf = _buf;
			r.len = _len;
			if (fjIo(r) != _len)
				_err = true;
			_len = 0;
		}
		return !_err;
	}

	uint32 write(const void *dataPtr, uint32 dataSize) override {
		const byte *src = (const byte *)dataPtr;
		for (uint32 i = 0; i < dataSize;) {
			uint32 n = MIN<uint32>(dataSize - i, kBufSize - _len);
			memcpy(_buf + _len, src + i, n);
			_len += n;
			i += n;
			if (_len == kBufSize)
				flush();
		}
		_pos += dataSize;
		return dataSize;
	}
};

Common::SeekableReadStream *fj_open_read(const char *name) {
	FjIoReq r = { FJ_IO_OPEN, -1, name };
	r.len = 0;
	int h = fjIo(r);
	if (h < 0)
		return 0;
	FjIoReq s = { FJ_IO_SIZE, h };
	int32 size = fjIo(s);
	return new FjReadStream(h, size);
}

Common::WriteStream *fj_open_write(const char *name) {
	FjIoReq r = { FJ_IO_OPEN, -1, name };
	r.len = 1;
	int h = fjIo(r);
	if (h < 0)
		return 0;
	return new FjWriteStream(h);
}

bool fj_file_exists(const char *name) {
	FjIoReq r = { FJ_IO_OPEN, -1, name };
	r.len = 0;
	int h = fjIo(r);
	if (h < 0)
		return false;
	FjIoReq c = { FJ_IO_CLOSE, h };
	fjIo(c);
	return true;
}

#pragma mark --- Save files ---

namespace Common {

OutSaveFile *SaveFileManager::openForSaving(const String &name, bool compress) {
	WriteStream *w = fj_open_write(name.c_str());
	return w ? new OutSaveFile(w) : 0;
}

InSaveFile *SaveFileManager::openForLoading(const String &name) {
	return fj_open_read(name.c_str());
}

// Only "<target>.s??"-style patterns are used (ScummEngine::listSavegames).
// There is no directory listing, so probe the first 20 slots.
StringArray SaveFileManager::listSavefiles(const String &pattern) {
	StringArray out;
	// Adafruit Arcade Machines: from the C string. ScummEngine::listSavegames()
	// builds its pattern as "loom.s99" with setChar('*') and setChar(0),
	// which leaves the size at 8, so lastChar() was the NUL, the '*' was
	// never stripped, and every slot was probed as "loom.s*NN": the game's
	// load screen came up empty however many saves there were.
	String prefix = pattern.c_str();
	while (!prefix.empty() && (prefix.lastChar() == '*' || prefix.lastChar() == '?'))
		prefix.deleteLastChar();
	for (int slot = 0; slot < 20; slot++) {
		String name = String::format("%s%02d", prefix.c_str(), slot);
		if (fj_file_exists(name.c_str()))
			out.push_back(name);
	}
	return out;
}

} // End of namespace Common

#pragma mark --- Timers ---

namespace Common {

TimerManager::TimerManager() {
	memset(_slots, 0, sizeof(_slots));
}

bool TimerManager::installTimerProc(TimerProc proc, int32 interval, void *refCon, const char *id) {
	for (int i = 0; i < kMaxTimers; i++) {
		if (!_slots[i].proc) {
			_slots[i].proc = proc;
			_slots[i].refCon = refCon;
			_slots[i].interval = interval;
			_slots[i].counter = interval;
			return true;
		}
	}
	return false;
}

void TimerManager::removeTimerProc(TimerProc proc) {
	for (int i = 0; i < kMaxTimers; i++) {
		if (_slots[i].proc == proc)
			_slots[i].proc = 0;
	}
}

void TimerManager::advance(uint32 msecs) {
	for (int i = 0; i < kMaxTimers; i++) {
		Slot &s = _slots[i];
		if (!s.proc)
			continue;
		s.counter -= msecs * 1000;
		while (s.proc && s.counter <= 0) {
			s.counter += s.interval;
			s.proc(s.refCon);
		}
	}
}

} // End of namespace Common

#pragma mark --- Events ---

namespace Common {

bool EventManager::pollEvent(Event &event) {
	if (_head == _tail)
		return false;
	event = _queue[_head];
	_head = (_head + 1) % kQueueSize;
	if (event.type == EVENT_MOUSEMOVE || event.type == EVENT_LBUTTONDOWN || event.type == EVENT_LBUTTONUP ||
	    event.type == EVENT_RBUTTONDOWN || event.type == EVENT_RBUTTONUP)
		_mousePos = event.mouse;
	if (event.type == EVENT_LBUTTONDOWN)
		_buttonState |= LBUTTON;
	else if (event.type == EVENT_LBUTTONUP)
		_buttonState &= ~LBUTTON;
	else if (event.type == EVENT_RBUTTONDOWN)
		_buttonState |= RBUTTON;
	else if (event.type == EVENT_RBUTTONUP)
		_buttonState &= ~RBUTTON;
	return true;
}

void EventManager::pushEvent(const Event &event) {
	int next = (_tail + 1) % kQueueSize;
	if (next == _head)
		return; // full: drop
	_queue[_tail] = event;
	_tail = next;
}

} // End of namespace Common

#pragma mark --- ConfMan ---

namespace Common {

static const char *confFind(const String &key) {
	for (int i = 0; i < fj->numConfig; i++) {
		if (key == fj->config[i].key)
			return fj->config[i].value;
	}
	return 0;
}

bool ConfigManager::hasKey(const String &key) const {
	return confFind(key) != 0;
}

String ConfigManager::get(const String &key) const {
	const char *v = confFind(key);
	return v ? String(v) : String();
}

int ConfigManager::getInt(const String &key) const {
	const char *v = confFind(key);
	return v ? atoi(v) : 0;
}

bool ConfigManager::getBool(const String &key) const {
	const char *v = confFind(key);
	return v && (!strcmp(v, "true") || !strcmp(v, "1") || !strcmp(v, "yes"));
}

void ConfigManager::setInt(const String &key, int value) {
	fj_core_config(key.c_str(), String::format("%d", value).c_str());
}

void ConfigManager::setBool(const String &key, bool value) {
	fj_core_config(key.c_str(), value ? "true" : "false");
}

} // End of namespace Common

#pragma mark --- Palette, screen, cursor ---

void fjMarkAllDirty() {
	memset(fj->dirty, 1, sizeof(fj->dirty));
}

void PaletteManager::setPalette(const byte *colors, uint start, uint num) {
	for (uint i = start; i < start + num && i < 256; i++, colors += 3) {
		byte r = colors[0], g = colors[1], b = colors[2];
		fj->palette[i * 3] = r;
		fj->palette[i * 3 + 1] = g;
		fj->palette[i * 3 + 2] = b;
		fj->lut16[i] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
		fj->lut8[i] = (r & 0xE0) | ((g & 0xE0) >> 3) | (b >> 6);
	}
	fjMarkAllDirty();
}

void PaletteManager::grabPalette(byte *colors, uint start, uint num) const {
	memcpy(colors, fj->palette + start * 3, num * 3);
}

OSystem::OSystem() : _paletteManager(0), _eventManager(0), _savefileManager(0), _mixer(0), _audiocdManager(0),
	_timerManager(0), _width(FJ_SCREEN_W), _height(FJ_SCREEN_H),
	_screenFormat(Graphics::PixelFormat::createFormatCLUT8()) {
}

Common::List<Graphics::PixelFormat> OSystem::getSupportedFormats() const {
	Common::List<Graphics::PixelFormat> list;
	list.push_back(Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
	list.push_back(Graphics::PixelFormat::createFormatCLUT8());
	return list;
}

#ifndef FJ_SCREEN_ALLOC   // Adafruit Arcade Machines: see fj_core.cpp
#define FJ_SCREEN_ALLOC(n) calloc((n), 1)
#define FJ_SCREEN_FREE(p) free(p)
#endif

void OSystem::initSize(uint width, uint height, const Graphics::PixelFormat *format) {
	if (width != FJ_SCREEN_W || height != FJ_SCREEN_H)
		warning("initSize %dx%d: only 320x200 is supported", width, height);
	// Only RGB565 (Loom PC-Engine) or CLUT8.
	int bpp = (format && format->bytesPerPixel == 2) ? 2 : 1;
	if (bpp != fj->screenBpp) {
		byte *s = (byte *)FJ_SCREEN_ALLOC(FJ_SCREEN_W * FJ_SCREEN_H * bpp);
		if (!s)
			error("initSize: out of memory");
		FJ_SCREEN_FREE(fj->screen);
		fj->screen = s;
		fj->screenBpp = bpp;
	}
	_screenFormat = bpp == 2 ? Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0) : Graphics::PixelFormat::createFormatCLUT8();
	fjMarkAllDirty();
}

void OSystem::copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
	const byte *src = (const byte *)buf;
	const int bpp = fj->screenBpp;
	if (x < 0) {
		w += x;
		src -= x * bpp;
		x = 0;
	}
	if (y < 0) {
		h += y;
		src -= y * pitch;
		y = 0;
	}
	if (x + w > FJ_SCREEN_W)
		w = FJ_SCREEN_W - x;
	if (y + h > FJ_SCREEN_H)
		h = FJ_SCREEN_H - y;
	if (w <= 0 || h <= 0)
		return;
	byte *dst = fj->screen + (y * FJ_SCREEN_W + x) * bpp;
	for (int i = 0; i < h; i++) {
		memcpy(dst, src, w * bpp);
		fj->dirty[y + i] = 1;
		dst += FJ_SCREEN_W * bpp;
		src += pitch;
	}
}

Graphics::Surface *OSystem::lockScreen() {
	fj->lockSurface.init(FJ_SCREEN_W, FJ_SCREEN_H, FJ_SCREEN_W * fj->screenBpp, fj->screen, _screenFormat);
	return &fj->lockSurface;
}

void OSystem::unlockScreen() {
	fjMarkAllDirty();
}

void OSystem::fillScreen(uint32 col) {
	if (fj->screenBpp == 2) {
		uint16 *p = (uint16 *)fj->screen;
		for (int i = 0; i < FJ_SCREEN_W * FJ_SCREEN_H; i++)
			p[i] = (uint16)col;
	} else {
		memset(fj->screen, col, FJ_SCREEN_W * FJ_SCREEN_H);
	}
	fjMarkAllDirty();
}

void OSystem::updateScreen() {
	// The host draws dirty rows when the frame ends.
}

void OSystem::setShakePos(int shakeXOffset, int shakeYOffset) {
	if (fj->shakeY != shakeYOffset) {
		fj->shakeY = shakeYOffset;
		fjMarkAllDirty();
	}
}

bool OSystem::showMouse(bool visible) {
	bool last = fj->cursorVisible;
	fj->cursorVisible = visible;
	return last;
}

void OSystem::warpMouse(int x, int y) {
	fj->mouseX = fj->sentX = x;
	fj->mouseY = fj->sentY = y;
}

void OSystem::setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor, bool dontScale, const Graphics::PixelFormat *format) {
	// The cursor is in the screen's format (ScummEngine::updateCursor
	// passes no format here).
	const int bpp = fj->screenBpp;
	if (w * h > (uint)(fj->cursorW * fj->cursorH) || !fj->cursor) {
		free(fj->cursor);
		fj->cursor = (byte *)malloc(w * h * bpp);
	}
	if (!fj->cursor) {
		fj->cursorW = fj->cursorH = 0;
		return;
	}
	memcpy(fj->cursor, buf, w * h * bpp);
	fj->cursorW = w;
	fj->cursorH = h;
	fj->cursorHX = hotspotX;
	fj->cursorHY = hotspotY;
	fj->cursorKey = keycolor;
}

void OSystem::setCursorPalette(const byte *colors, uint start, uint num) {
}

bool Graphics::CursorManager::isVisible() {
	return fj->cursorVisible;
}

#pragma mark --- Time ---

uint32 OSystem::getMillis(bool skipRecord) {
	return (uint32)(fj->clockUs / 1000);
}

void fjYieldFrame() {
	fj->yield = FJ_YIELD_FRAME;
	fj_coro_yield();
}

// On the coroutine every delay ends the frame: the host advances the clock
// by 1/60 s per frame, so delays are rounded up to whole frames.
void OSystem::delayMillis(uint msecs) {
	if (!fj_coro_active())
		return;
	uint64 until = fj->clockUs + (uint64)msecs * 1000;
	do {
		fjYieldFrame();
	} while (fj->clockUs < until);
}

void OSystem::getTimeAndDate(TimeDate &t) const {
	memset(&t, 0, sizeof(t));
	t.tm_year = 126;
	t.tm_mday = 1;
}

void OSystem::fatalError() {
	error("fatal error");
}

void OSystem::logMessage(int type, const char *message) {
	fjLog(message);
}

#pragma mark --- error(), warning() ---

void fjHostAbort();

void NORETURN_PRE error(const char *s, ...) {
	char buf[STRINGBUFLEN];
	va_list va;
	va_start(va, s);
	vsnprintf(buf, sizeof(buf), s, va);
	va_end(va);

	if (g_engine) {
		char full[STRINGBUFLEN];
		g_engine->errorString(buf, full, sizeof(full));
		Common::strlcpy(buf, full, sizeof(buf));
	}
	Common::strlcpy(fj->error, buf, sizeof(fj->error));
	fj->dead = true;

	Common::String line = Common::String::format("error: %s", buf);
	fjLog(line.c_str());
	if (fj_coro_active()) {
		// The engine is finished; the host never resumes it again.
		for (;;) {
			fj->yield = FJ_YIELD_DEAD;
			fj_coro_yield();
		}
	}
	// Host side (mixer callbacks): unwind to fj_core_frame.
	fjHostAbort();
}

void warning(const char *s, ...) {
	char buf[STRINGBUFLEN];
	va_list va;
	va_start(va, s);
	int n = snprintf(buf, sizeof(buf), "warning: ");
	vsnprintf(buf + n, sizeof(buf) - n, s, va);
	va_end(va);
	fjLog(buf);
}

namespace Scumm {
void debugC(int level, const char *s, ...) {
}
}

#pragma mark --- Engine ---

static bool engineQuit;

Engine::Engine(OSystem *syst)
	: _system(syst),
	  _mixer(syst->getMixer()),
	  _timer(syst->getTimerManager()),
	  _eventMan(syst->getEventManager()),
	  _saveFileMan(syst->getSavefileManager()),
	  _targetName(ConfMan.get("gameid")),
	  _pauseLevel(0),
	  _pauseStartTime(0),
	  _engineStartTime(syst->getMillis()) {
	g_engine = this;
	engineQuit = false;
}

void Engine::errorString(const char *buf1, char *buf2, int size) {
	Common::strlcpy(buf2, buf1, size);
}

void Engine::quitGame() {
	engineQuit = true;
}

bool Engine::shouldQuit() {
	return engineQuit;
}

void Engine::pauseEngine(bool pause) {
	if (pause)
		_pauseLevel++;
	else
		_pauseLevel--;
	if (_pauseLevel == 1 && pause) {
		_pauseStartTime = _system->getMillis();
		pauseEngineIntern(true);
	} else if (_pauseLevel == 0) {
		pauseEngineIntern(false);
		_engineStartTime += _system->getMillis() - _pauseStartTime;
		_pauseStartTime = 0;
	}
}

uint32 Engine::getTotalPlayTime() const {
	if (!_pauseLevel)
		return _system->getMillis() - _engineStartTime;
	return _pauseStartTime - _engineStartTime;
}

void Engine::setTotalPlayTime(uint32 time) {
	const uint32 currentTime = _system->getMillis();
	if (_pauseLevel != 0)
		_engineStartTime = _pauseStartTime - time;
	else
		_engineStartTime = currentTime - time;
}
