#include "../backend/fj_arduino.h" // Adafruit Arcade Machines build mode
/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "../common/system.h"
#include "../common/util.h"
#include "../graphics/cursorman.h"
#include "charset.h"
#include "object.h"
#include "scumm.h"
#include "scumm_v3.h"
#include "scumm_v5.h"

namespace Scumm {

/*
 * Mouse cursor cycle colors (for the default crosshair).
 */
static const byte default_v1_cursor_colors[4] = {
	1, 1, 12, 11
};

static const uint16 default_pce_cursor_colors[4] = {
	0x01FF, 0x01FF, 0x016D, 0x0092
};

static const byte default_cursor_colors[4] = {
	15, 15, 7, 8
};

// 2bpp
static const uint16 default_he_cursor[128] = {
	0x0a80, 0x0000, 0x0000, 0x0000, 0x2568, 0x0000, 0x0000, 0x0000,
	0x9556, 0x8000, 0x0000, 0x0000, 0x9955, 0x6800, 0x0000, 0x0000,
	0x9955, 0x5680, 0x0000, 0x0000, 0x2655, 0x5568, 0x0000, 0x0000,
	0x2695, 0x5556, 0x8000, 0x0000, 0x0995, 0x5555, 0x6800, 0x0000,
	0x09a5, 0x5555, 0x5600, 0x0000, 0x0269, 0x5555, 0x5580, 0x0000,
	0x0269, 0x5555, 0x5560, 0x0000, 0x009a, 0x5555, 0x5560, 0x0000,
	0x009a, 0x9555, 0x5580, 0x0000, 0x0026, 0x9555, 0x5600, 0x0000,
	0x0026, 0xa555, 0x5580, 0x0000, 0x0009, 0xa955, 0x5560, 0x0000,
	0x0009, 0xa9a9, 0x5558, 0x0000, 0x0002, 0x6aaa, 0x5558, 0x0000,
	0x0002, 0x6a9a, 0xaa58, 0x0000, 0x0000, 0x9a66, 0xaa60, 0x0000,
	0x0000, 0x2589, 0xa980, 0x0000, 0x0000, 0x0a02, 0x5600, 0x0000,
	0x0000, 0x0000, 0xa800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

static const byte default_v6_cursor[] = {
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0x00,0x0F,0x00, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0x00,0x0F,0x00, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0x00,0x0F,0x00, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0x00,0x0F,0x00, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0x00,0x0F,0x00, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x0F,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0xFF,
	0x0F,0x0F,0x0F,0x0F,0x0F,0x0F, 0x0F,0x0F,0x0F, 0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0xFF,
	0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x0F,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0x00,0x0F,0x00, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0x00,0x0F,0x00, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0x00,0x0F,0x00, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0x00,0x0F,0x00, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0x00,0x0F,0x00, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
};

void ScummEngine_v5::animateCursor() {
	if (_cursor.animate) {
		if (!(_cursor.animateIndex & 0x1)) {
			setBuiltinCursor((_cursor.animateIndex >> 1) & 3);
		}
		_cursor.animateIndex++;
	}
}

void ScummEngine::updateCursor() {
	int transColor = (_game.heversion >= 80) ? 5 : 255;
	CursorMan.replaceCursor(_grabbedCursor, _cursor.width, _cursor.height,
							_cursor.hotspotX, _cursor.hotspotY,
							(_game.platform == Common::kPlatformNES ? _grabbedCursor[63] : transColor),
							(_game.heversion == 70 ? true : false));
}

void ScummEngine::setCursorFromBuffer(const byte *ptr, int width, int height, int pitch) {
	uint size;
	byte *dst;

	size = width * height * _bytesPerPixel;
	if (size > sizeof(_grabbedCursor))
		error("grabCursor: grabbed cursor too big");

	_cursor.width = width;
	_cursor.height = height;
	_cursor.animate = 0;

	dst = _grabbedCursor;
	for (; height; height--) {
		memcpy(dst, ptr, width * _bytesPerPixel);
		dst += width * _bytesPerPixel;
		ptr += pitch;
	}

	updateCursor();
}


void ScummEngine_v5::redefineBuiltinCursorFromChar(int index, int chr) {
	// Cursor image in both Loom versions are based on images from charset.
	// This function is *only* supported for Loom!
	assert(_game.id == GID_LOOM);

	assert(index >= 0 && index < 4);

//	const int oldID = _charset->getCurID();

	uint16 *ptr = _cursorImages[index];
	int h;

	if (index == 1 && _game.platform == Common::kPlatformPCEngine) {
		uint16 cursorPCE[] = {
			0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
			0xF180, 0xF800, 0x8C00, 0x0C00, 0x0600, 0x0600, 0x0300, 0x0000
		};

		for (h = 0; h < ARRAYSIZE(cursorPCE); h++) {
			*ptr++ = cursorPCE[h];
		}
	} else {
		if (_game.version == 3) {
			_charset->setCurID(0);
		} else if (_game.version >= 4) {
			_charset->setCurID(1);
		}

		Graphics::Surface s;
		byte buf[16*17];
		memset(buf, 123, 16*17);
		s.init(_charset->getCharWidth(chr), _charset->getFontHeight(),
		       _charset->getCharWidth(chr), buf,
		       Graphics::PixelFormat::createFormatCLUT8());
		// s.h = 17 for FM-TOWNS Loom Japanese. Fixes bug #1166917
		assert(s.w <= 16 && s.h <= 17);

		_charset->drawChar(chr, s, 0, 0);

		memset(ptr, 0, 17 * sizeof(uint16));
		for (h = 0; h < s.h; h++) {
			for (int w = 0; w < s.w; w++) {
				if (buf[s.pitch * h + w] != 123)
					*ptr |= 1 << (15 - w);
			}
			ptr++;
		}
	}

//	_charset->setCurID(oldID);
}

void ScummEngine_v5::redefineBuiltinCursorHotspot(int index, int x, int y) {
	// Cursor image in both Looms are based on images from charset.
	// This function is *only* supported for Loom!
	assert(_game.id == GID_LOOM);

	assert(index >= 0 && index < 4);

	_cursorHotspots[index * 2] = x;
	_cursorHotspots[index * 2 + 1] = y;
}

void ScummEngine_v5::resetCursors() {
	static const uint16 default_cursor_images[4][16] = {
		/* cross-hair */
		{ 0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0000, 0x7e3f,
		  0x0000, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0000 },
		/* hourglass */
		{ 0x0000, 0x7ffe, 0x6006, 0x300c, 0x1818, 0x0c30, 0x0660, 0x03c0,
		  0x0660, 0x0c30, 0x1998, 0x33cc, 0x67e6, 0x7ffe, 0x0000, 0x0000 },
		/* arrow */
		{ 0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7c00, 0x7e00, 0x7f00,
		  0x7f80, 0x78c0, 0x7c00, 0x4600, 0x0600, 0x0300, 0x0300, 0x0180 },
		/* hand */
		{ 0x1e00, 0x1200, 0x1200, 0x1200, 0x1200, 0x13ff, 0x1249, 0x1249,
		  0xf249, 0x9001, 0x9001, 0x9001, 0x8001, 0x8001, 0x8001, 0xffff },
	};

	static const byte default_cursor_hotspots[10] = {
		8, 7,   8, 7,   1, 1,   5, 0,
		8, 7, //zak256
	};


	for (int i = 0; i < 4; i++) {
		memcpy(_cursorImages[i], default_cursor_images[i], 32);
	}
	memcpy(_cursorHotspots, default_cursor_hotspots, 8);

}

void ScummEngine_v5::setBuiltinCursor(int idx) {
	int i, j;
	uint16 color;
	const uint16 *src = _cursorImages[_currentCursor];

	if (_outputPixelFormat.bytesPerPixel == 2) {
		if (_game.id == GID_LOOM && _game.platform == Common::kPlatformPCEngine) {
			byte r, g, b;
			colorPCEToRGB(default_pce_cursor_colors[idx], &r, &g, &b);
			color = get16BitColor(r, g, b);
		} else {
			color = _16BitPalette[default_cursor_colors[idx]];
		}

		for (i = 0; i < 1024; i++)
			WRITE_UINT16(_grabbedCursor + i * 2, 0xFF);
	} else {
		// Indy4 Amiga uses its own color set for the cursor image.
		// This is patchwork code to make the cursor flash in correct colors.
		if (_game.platform == Common::kPlatformAmiga && _game.id == GID_INDY4) {
			static const uint8 indy4AmigaColors[4] = {
				252, 252, 253, 254
			};
			color = indy4AmigaColors[idx];
		} else {
			color = default_cursor_colors[idx];
		}
		memset(_grabbedCursor, 0xFF, sizeof(_grabbedCursor));
	}

	_cursor.hotspotX = _cursorHotspots[2 * _currentCursor] * _textSurfaceMultiplier;
	_cursor.hotspotY = _cursorHotspots[2 * _currentCursor + 1] * _textSurfaceMultiplier;
	_cursor.width = 16 * _textSurfaceMultiplier;
	_cursor.height = 16 * _textSurfaceMultiplier;

	int scl = _outputPixelFormat.bytesPerPixel * _textSurfaceMultiplier;

	for (i = 0; i < 16; i++) {
		for (j = 0; j < 16; j++) {
			if (src[i] & (1 << j)) {
				byte *dst1 = _grabbedCursor + 16 * scl * i * _textSurfaceMultiplier + (15 - j) * scl;
				byte *dst2 = (_textSurfaceMultiplier == 2) ? dst1 + 16 * scl : dst1;
				if (_outputPixelFormat.bytesPerPixel == 2) {
					for (int b = 0; b < scl; b += 2) {
						*((uint16 *)dst1) = *((uint16 *)dst2) = color;
						dst1 += 2;
						dst2 += 2;
					}
				} else {
					for (int b = 0; b < scl; b++)
						*dst1++ = *dst2++ = color;
				}
			}
		}
	}

	updateCursor();
}

} // End of namespace Scumm
