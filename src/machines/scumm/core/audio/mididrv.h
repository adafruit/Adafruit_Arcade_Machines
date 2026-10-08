/* fruitjam-scumm: only the music type enums from ScummVM's mididrv.h.
 * There are no MIDI drivers; the music players used here (Player_AD,
 * Player_V2) generate audio themselves.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef AUDIO_MIDIDRV_H
#define AUDIO_MIDIDRV_H

#include "../common/scummsys.h"

enum MusicType {
	MT_INVALID = -1,
	MT_AUTO = 0,
	MT_NULL,
	MT_PCSPK,
	MT_PCJR,
	MT_CMS,
	MT_ADLIB,
	MT_C64,
	MT_AMIGA,
	MT_APPLEIIGS,
	MT_TOWNS,
	MT_PC98,
	MT_SEGACD,
	MT_GM,
	MT_MT32,
	MT_GS
};

enum MidiDriverFlags {
	MDT_NONE        = 0,
	MDT_PCSPK       = 1 << 0,
	MDT_CMS         = 1 << 1,
	MDT_PCJR        = 1 << 2,
	MDT_ADLIB       = 1 << 3,
	MDT_C64         = 1 << 4,
	MDT_AMIGA       = 1 << 5,
	MDT_APPLEIIGS   = 1 << 6,
	MDT_TOWNS       = 1 << 7,
	MDT_PC98        = 1 << 8,
	MDT_SEGACD      = 1 << 9,
	MDT_MIDI        = 1 << 10,
	MDT_PREFER_MT32 = 1 << 11,
	MDT_PREFER_GM   = 1 << 12,
	MDT_PREFER_FLUID= 1 << 13
};

#endif
