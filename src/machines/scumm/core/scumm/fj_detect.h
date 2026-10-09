/* fruitjam-scumm - SCUMM v3/v4 on the Adafruit Fruit Jam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef SCUMM_FJ_DETECT_H
#define SCUMM_FJ_DETECT_H

#include "detection.h"

namespace Scumm {

// Detect the game in the current game folder. Returns 0 and fills dr, or
// returns why nothing was found.
const char *fjDetectGame(DetectorResult &dr);

} // End of namespace Scumm

#endif
