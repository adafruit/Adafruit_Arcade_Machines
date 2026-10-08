/* fruitjam-scumm: palette goes straight to the backend's 256-entry LUT.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef GRAPHICS_PALETTE_H
#define GRAPHICS_PALETTE_H

#include "../common/scummsys.h"

class PaletteManager {
public:
	void setPalette(const byte *colors, uint start, uint num);
	void grabPalette(byte *colors, uint start, uint num) const;
};

#endif
