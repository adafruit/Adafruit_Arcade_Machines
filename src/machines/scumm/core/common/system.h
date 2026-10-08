/* fruitjam-scumm - SCUMM v3/v4 on the Adafruit Fruit Jam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * A much smaller stand-in for ScummVM's common/system.h. There is exactly one
 * backend (src/backend/osystem.cpp), so OSystem is a concrete class with only
 * the calls the SCUMM v3/v4 engine makes.
 */

#ifndef COMMON_SYSTEM_H
#define COMMON_SYSTEM_H

#include "scummsys.h"
#include "list.h"
#include "rect.h"
#include "../graphics/pixelformat.h"

namespace Audio {
class Mixer;
}

namespace Common {
class EventManager;
class SaveFileManager;
class TimerManager;
}

namespace Graphics {
struct Surface;
}

class AudioCDManager;
class PaletteManager;

struct TimeDate {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
};

class OSystem {
public:
	enum Feature {
		kFeatureFullscreenMode,
		kFeatureAspectRatioCorrection,
		kFeatureFilteringMode,
		kFeatureVirtualKeyboard,
		kFeatureCursorPalette,
		kFeatureOverlaySupportsAlpha,
		kFeatureIconifyWindow,
		kFeatureOpenGL,
		kFeatureVSync,
		kFeatureFullscreenToggleKeepsContext,
		kFeatureClipboardSupport,
		kFeatureOpenUrl,
		kFeatureTouchpadMode,
		kFeatureSwapMenuAndBackButtons,
		kFeatureKbdMouseSpeed,
		kFeatureJoystickDeadzone,
		kFeatureShader,
		kFeatureDisplayLogFile,
		kFeatureNoQuit
	};

	OSystem();

	bool hasFeature(Feature f) { return false; }
	void setFeatureState(Feature f, bool enable) {}
	bool getFeatureState(Feature f) { return false; }

	Graphics::PixelFormat getScreenFormat() const { return _screenFormat; }
	Common::List<Graphics::PixelFormat> getSupportedFormats() const;

	void initSize(uint width, uint height, const Graphics::PixelFormat *format = NULL);
	int16 getHeight() { return _height; }
	int16 getWidth() { return _width; }

	PaletteManager *getPaletteManager() { return _paletteManager; }
	void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h);
	Graphics::Surface *lockScreen();
	void unlockScreen();
	void fillScreen(uint32 col);
	void updateScreen();
	void setShakePos(int shakeXOffset, int shakeYOffset);
	void setFocusRectangle(const Common::Rect &rect) {}
	void clearFocusRectangle() {}

	bool showMouse(bool visible);
	void warpMouse(int x, int y);
	void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor, bool dontScale = false, const Graphics::PixelFormat *format = NULL);
	void setCursorPalette(const byte *colors, uint start, uint num);

	uint32 getMillis(bool skipRecord = false);
	void delayMillis(uint msecs);
	void getTimeAndDate(TimeDate &t) const;

	Common::EventManager *getEventManager() { return _eventManager; }
	Common::SaveFileManager *getSavefileManager() { return _savefileManager; }
	Common::TimerManager *getTimerManager() { return _timerManager; }
	Audio::Mixer *getMixer() { return _mixer; }
	AudioCDManager *getAudioCDManager() { return _audiocdManager; }

	void quit() {}
	void fatalError();
	void logMessage(int type, const char *message);

	PaletteManager *_paletteManager;
	Common::EventManager *_eventManager;
	Common::SaveFileManager *_savefileManager;
	Audio::Mixer *_mixer;
	AudioCDManager *_audiocdManager;
	Common::TimerManager *_timerManager;
	int16 _width, _height;
	Graphics::PixelFormat _screenFormat;
};

extern OSystem *g_system;

#endif
