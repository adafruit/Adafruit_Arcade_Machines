/* fruitjam-scumm: CursorMan forwards to the backend, which draws one
 * cursor over the picture. No cursor stack.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef GRAPHICS_CURSORMAN_H
#define GRAPHICS_CURSORMAN_H

#include "../common/scummsys.h"
#include "../common/system.h"
#include "pixelformat.h"

namespace Graphics {

class CursorManager {
public:
	bool isVisible();
	bool showMouse(bool visible) { return g_system->showMouse(visible); }
	void replaceCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor, bool dontScale = false, const Graphics::PixelFormat *format = NULL) {
		g_system->setMouseCursor(buf, w, h, hotspotX, hotspotY, keycolor, dontScale, format);
	}
	bool supportsCursorPalettes() { return false; }
	void disableCursorPalette(bool disable) {}
	void replaceCursorPalette(const byte *colors, uint start, uint num) {}
};

extern CursorManager CursorMan;

} // End of namespace Graphics

using Graphics::CursorMan;

#endif
