/* fruitjam-scumm */
#ifndef ENGINES_UTIL_H
#define ENGINES_UTIL_H

#include "../common/system.h"

inline void initGraphics(int width, int height, const Graphics::PixelFormat *format = NULL) {
	g_system->initSize(width, height, format);
}

#endif
