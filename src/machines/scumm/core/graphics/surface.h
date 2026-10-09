/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// fruitjam-scumm: ScummVM's Surface with the conversion, scaling and line
// drawing parts removed. 8-bit surfaces only in practice.

#ifndef GRAPHICS_SURFACE_H
#define GRAPHICS_SURFACE_H

#include "../common/scummsys.h"
#include "../common/rect.h"
#include "pixelformat.h"

namespace Graphics {

struct Surface {
	uint16 w;
	uint16 h;
	uint16 pitch;

protected:
	void *pixels;

public:
	PixelFormat format;

	Surface() : w(0), h(0), pitch(0), pixels(0), format() {
	}

	inline const void *getPixels() const {
		return pixels;
	}

	inline void *getPixels() {
		return pixels;
	}

	void setPixels(void *newPixels) { pixels = newPixels; }

	inline const void *getBasePtr(int x, int y) const {
		return (const byte *)(pixels) + y * pitch + x * format.bytesPerPixel;
	}

	inline void *getBasePtr(int x, int y) {
		return static_cast<byte *>(pixels) + y * pitch + x * format.bytesPerPixel;
	}

	void create(uint16 width, uint16 height, const PixelFormat &format);
	void free();
	void init(uint16 width, uint16 height, uint16 pitch, void *pixels, const PixelFormat &format);
	void copyRectToSurface(const void *buffer, int srcPitch, int destX, int destY, int width, int height);
	void hLine(int x, int y, int x2, uint32 color);
	void vLine(int x, int y, int y2, uint32 color);
	void fillRect(Common::Rect r, uint32 color);
	void frameRect(const Common::Rect &r, uint32 color);
	void move(int dx, int dy, int height);
};

} // End of namespace Graphics

#endif
