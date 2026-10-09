#include "../backend/fj_arduino.h" // Adafruit Arcade Machines build mode
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

#include "../common/util.h"
#include "../common/textconsole.h"
#include "surface.h"

namespace Graphics {

void Surface::create(uint16 width, uint16 height, const PixelFormat &f) {
	free();

	w = width;
	h = height;
	format = f;
	pitch = w * format.bytesPerPixel;

	if (width && height) {
		pixels = calloc(width * height, format.bytesPerPixel);
		assert(pixels);
	}
}

void Surface::free() {
	::free(pixels);
	pixels = 0;
	w = h = pitch = 0;
	format = PixelFormat();
}

void Surface::init(uint16 width, uint16 height, uint16 newPitch, void *newPixels, const PixelFormat &f) {
	w = width;
	h = height;
	pitch = newPitch;
	pixels = newPixels;
	format = f;
}

void Surface::copyRectToSurface(const void *buffer, int srcPitch, int destX, int destY, int width, int height) {
	assert(buffer);

	assert(destX >= 0 && destX < w);
	assert(destY >= 0 && destY < h);
	assert(height > 0 && destY + height <= h);
	assert(width > 0 && destX + width <= w);

	// Copy buffer data to internal buffer
	const byte *src = (const byte *)buffer;
	byte *dst = (byte *)getBasePtr(destX, destY);
	for (int i = 0; i < height; i++) {
		memcpy(dst, src, width * format.bytesPerPixel);
		src += srcPitch;
		dst += pitch;
	}
}

void Surface::hLine(int x, int y, int x2, uint32 color) {
	fillRect(Common::Rect(MIN(x, x2), y, MAX(x, x2) + 1, y + 1), color);
}

void Surface::vLine(int x, int y, int y2, uint32 color) {
	fillRect(Common::Rect(x, MIN(y, y2), x + 1, MAX(y, y2) + 1), color);
}

void Surface::fillRect(Common::Rect r, uint32 color) {
	r.clip(w, h);

	if (!r.isValidRect())
		return;

	int width = r.width();
	int height = r.height();

	if (format.bytesPerPixel == 1) {
		byte *ptr = (byte *)getBasePtr(r.left, r.top);
		while (height--) {
			memset(ptr, (byte)color, width);
			ptr += pitch;
		}
	} else if (format.bytesPerPixel == 2) {
		uint16 *row = (uint16 *)getBasePtr(r.left, r.top);
		while (height--) {
			for (int i = 0; i < width; i++)
				row[i] = (uint16)color;
			row = (uint16 *)((byte *)row + pitch);
		}
	} else {
		error("Surface::fillRect: bytesPerPixel must be 1 or 2");
	}
}

void Surface::frameRect(const Common::Rect &r, uint32 color) {
	hLine(r.left, r.top, r.right - 1, color);
	hLine(r.left, r.bottom - 1, r.right - 1, color);
	vLine(r.left, r.top, r.bottom - 1, color);
	vLine(r.right - 1, r.top, r.bottom - 1, color);
}

// Scroll the top `height` lines by (dx, dy). 8-bit surfaces only.
void Surface::move(int dx, int dy, int height) {
	if ((dx == 0 && dy == 0) || height <= 0)
		return;
	if (format.bytesPerPixel != 1)
		error("Surface::move: 8-bit surfaces only");

	byte *base = (byte *)pixels;
	if (dy > 0) {
		for (int y = height - 1; y >= dy; y--)
			memcpy(base + y * pitch, base + (y - dy) * pitch, w);
	} else if (dy < 0) {
		for (int y = 0; y < height + dy; y++)
			memcpy(base + y * pitch, base + (y - dy) * pitch, w);
	}
	if (dx > 0) {
		for (int y = 0; y < height; y++)
			memmove(base + y * pitch + dx, base + y * pitch, w - dx);
	} else if (dx < 0) {
		for (int y = 0; y < height; y++)
			memmove(base + y * pitch, base + y * pitch - dx, w + dx);
	}
}

} // End of namespace Graphics
