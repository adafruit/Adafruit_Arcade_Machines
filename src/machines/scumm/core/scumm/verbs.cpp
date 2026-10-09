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

#include "actor.h"
#include "charset.h"
#include "object.h"
#include "resource.h"
#include "verbs.h"

namespace Scumm {

enum {
	kInventoryUpArrow = 4,
	kInventoryDownArrow = 5,
	kSentenceLine = 6
};

struct VerbSettings {
	int id;
	int x_pos;
	int y_pos;
	const char *name;
};

static const VerbSettings v0VerbTable_English[] = {
	{kVerbOpen,     8, 0, "Open"},
	{kVerbClose,    8, 1, "Close"},
	{kVerbGive,     0, 2, "Give"},
	{kVerbTurnOn,  32, 0, "Turn on"},
	{kVerbTurnOff, 32, 1, "Turn off"},
	{kVerbFix,     32, 2, "Fix"},
	{kVerbNewKid,  24, 0, "New Kid"},
	{kVerbUnlock,  24, 1, "Unlock"},
	{kVerbPush,     0, 0, "Push"},
	{kVerbPull,     0, 1, "Pull"},
	{kVerbUse,     24, 2, "Use"},
	{kVerbRead,     8, 2, "Read"},
	{kVerbWalkTo,  15, 0, "Walk to"},
	{kVerbPickUp,  15, 1, "Pick up"},
	{kVerbWhatIs,  15, 2, "What is"}
};

static const VerbSettings v0VerbTable_German[] = {
	{kVerbOpen,     7, 0, "$ffne"},
	{kVerbClose,   13, 1, "Schlie*e"},
	{kVerbGive,     0, 2, "Gebe"},
	{kVerbTurnOn,  37, 1, "Ein"},
	{kVerbTurnOff, 37, 0, "Aus"},
	{kVerbFix,     23, 1, "Repariere"},
	{kVerbNewKid,  34, 2, "Person"},
	{kVerbUnlock,  23, 0, "Schlie*e auf"},
	{kVerbPush,     0, 0, "Dr<cke"},
	{kVerbPull,     0, 1, "Ziehe"},
	{kVerbUse,     23, 2, "Benutz"},
	{kVerbRead,     7, 2, "Lese"},
	{kVerbWalkTo,  13, 0, "Gehe zu"},
	{kVerbPickUp,   7, 1, "Nimm"},
	{kVerbWhatIs,  13, 2, "Was ist"}
};

struct VerbDemo {
	int color;
	const char *str;
};
static VerbDemo v0DemoStr[] = {
	{7,  "        MANIAC MANSION DEMO DISK        "},
	{5,  "          from Lucasfilm Games          "},
	{5,  "    Copyright = 1987 by Lucasfilm Ltd.  "},
	{5,  "           All Rights Reserved.         "},
	{0,  "                                        "},
	{16, "       Press F7 to return to menu.      "}
};

void ScummEngine::redrawVerbs() {
	if (_game.version <= 2 && !(_userState & USERSTATE_IFACE_VERBS)) // Don't draw verbs unless active
		return;

	int i, verb = 0;
	if (_cursor.state > 0)
		verb = findVerbAtPos(_mouse.x, _mouse.y);

	// Iterate over all verbs.
	// Note: This is the correct order (at least for MI EGA, MI2, Full Throttle).
	// Do not change it! If you discover, based on disasm, that some game uses
	// another (e.g. the reverse) order here, you have to use an if/else construct
	// to add it as a special case!
	for (i = 0; i < _numVerbs; i++) {
		if (i == verb && _verbs[verb].hicolor)
			drawVerb(i, 1);
		else
			drawVerb(i, 0);
	}
	_verbMouseOver = verb;
}

void ScummEngine::handleMouseOver(bool updateInventory) {
	if (_completeScreenRedraw) {
		verbMouseOver(0);
	} else {
		if (_cursor.state > 0)
			verbMouseOver(findVerbAtPos(_mouse.x, _mouse.y));
	}
}


void ScummEngine::checkExecVerbs() {
	int i, over;
	VerbSlot *vs;

	if (_userPut <= 0 || _mouseAndKeyboardStat == 0)
		return;

	if (_mouseAndKeyboardStat < MBS_MAX_KEY) {
		/* Check keypresses */
		if (!(_game.id == GID_MONKEY && _game.platform == Common::kPlatformSegaCD)) {
			// This is disabled in the SegaCD version as the "vs->key" values setup
			// by script-17 conflict with the values expected by the generic keyboard
			// input script. See tracker item #1193185.
			vs = &_verbs[1];
			for (i = 1; i < _numVerbs; i++, vs++) {
				if (vs->verbid && vs->saveid == 0 && vs->curmode == 1) {
					if (_mouseAndKeyboardStat == vs->key) {
						// Trigger verb as if the user clicked it
						runInputScript(kVerbClickArea, vs->verbid, 1);
						return;
					}
				}
			}
		}

		if ((_game.id == GID_INDY4 || _game.id == GID_PASS) && _mouseAndKeyboardStat >= '0' && _mouseAndKeyboardStat <= '9') {
			// To support keyboard fighting in FOA, we need to remap the number keys.
			// FOA apparently expects PC scancode values (see script 46 if you want
			// to know where I got these numbers from). Oddly enough, the The Indy 3
			// part of the "Passport to Adventure" demo expects the same keyboard
			// mapping, even though the full game doesn't.
			static const int numpad[10] = {
				'0',
				335, 336, 337,
				331, 332, 333,
				327, 328, 329
			};
			_mouseAndKeyboardStat = numpad[_mouseAndKeyboardStat - '0'];
		}

		if (_game.platform == Common::kPlatformFMTowns && _game.version == 3) {
			// HACK: In the FM-TOWNS games Indy3, Loom and Zak the most significant bit is set for special keys
			// like F5 (=0x8005) or joystick buttons (mask 0xFE00, e.g. SELECT=0xFE40 for the save/load menu).
			// Hence the distinction with (_mouseAndKeyboardStat < MBS_MAX_KEY) between mouse- and key-events is not applicable
			// to this games, so we have to remap the special keys here.
			if (_mouseAndKeyboardStat == 319) {
				_mouseAndKeyboardStat = 0x8005;
			}
		}

		if ((_game.platform == Common::kPlatformFMTowns && _game.id == GID_ZAK) &&
			(_mouseAndKeyboardStat >= 315 && _mouseAndKeyboardStat <= 318)) {
			// Hack: Handle switching to a person via F1-F4 keys.
			// This feature isn't available in the scripts of the FM-TOWNS version.
			int fKey = _mouseAndKeyboardStat - 314;
			int switchSlot = getVerbSlot(36, 0);
			// check if switch-verb is enabled
			if (_verbs[switchSlot].curmode == 1) {
				// Check if person is available (see script 23 from ZAK_FM-TOWNS and script 4 from ZAK_PC).
				// Zak: Var[144 Bit 15], Annie: Var[145 Bit 0], Melissa: Var[145 Bit 1], Leslie: Var[145 Bit 2]
				if (!readVar(0x890E + fKey)) {
					runInputScript(kVerbClickArea, 36 + fKey, 0);
				}
			}
			return;
		}

		// Generic keyboard input
		runInputScript(kKeyClickArea, _mouseAndKeyboardStat, 1);
	} else if (_mouseAndKeyboardStat & MBS_MOUSE_MASK) {
		VirtScreen *zone = findVirtScreen(_mouse.y);
		const byte code = _mouseAndKeyboardStat & MBS_LEFT_CLICK ? 1 : 2;

		// This could be kUnkVirtScreen.
		// Fixes bug #1536932: "MANIACNES: Crash on click in speechtext-area"
		if (!zone)
			return;

		over = findVerbAtPos(_mouse.x, _mouse.y);
		if (over != 0) {
			// Verb was clicked
			runInputScript(kVerbClickArea, _verbs[over].verbid, code);
		} else {
			// Scene was clicked
			runInputScript((zone->number == kMainVirtScreen) ? kSceneClickArea : kVerbClickArea, 0, code);
		}
	}
}

void ScummEngine::verbMouseOver(int verb) {
	// Don't do anything unless verbs are active
	if (_game.version <= 2 && !(_userState & USERSTATE_IFACE_VERBS))
		return;

	if (_game.id == GID_FT)
		return;

	if (_verbMouseOver != verb) {
		if (_verbs[_verbMouseOver].type != kImageVerbType) {
			drawVerb(_verbMouseOver, 0);
			_verbMouseOver = verb;
		}

		if (_verbs[verb].type != kImageVerbType && _verbs[verb].hicolor) {
			drawVerb(verb, 1);
			_verbMouseOver = verb;
		}
	}
}

int ScummEngine::findVerbAtPos(int x, int y) const {
	if (!_numVerbs)
		return 0;

	VerbSlot *vs;
	int i = _numVerbs - 1;

	vs = &_verbs[i];
	do {
		if (vs->curmode != 1 || !vs->verbid || vs->saveid || y < vs->curRect.top || y >= vs->curRect.bottom)
			continue;
		if (vs->center) {
			if (x < -(vs->curRect.right - 2 * vs->curRect.left) || x >= vs->curRect.right)
				continue;
		} else {
			if (x < vs->curRect.left || x >= vs->curRect.right)
				continue;
		}

		return i;
	} while (--vs, --i);

	return 0;
}


void ScummEngine::drawVerb(int verb, int mode) {
	VerbSlot *vs;
	bool tmp;

	if (!verb)
		return;

	vs = &_verbs[verb];

	if (!vs->saveid && vs->curmode && vs->verbid) {
		if (vs->type == kImageVerbType) {
			drawVerbBitmap(verb, vs->curRect.left, vs->curRect.top);
			return;
		}

		restoreVerbBG(verb);

		_string[4].charset = vs->charset_nr;
		_string[4].xpos = vs->curRect.left;
		_string[4].ypos = vs->curRect.top;
		_string[4].right = _screenWidth - 1;
		_string[4].center = vs->center;

		if (vs->curmode == 2)
			_string[4].color = vs->dimcolor;
		else if (mode && vs->hicolor)
			_string[4].color = vs->hicolor;
		else
			_string[4].color = vs->color;

		// FIXME For the future: Indy3 and under inv scrolling
		/*
		   if (verb >= 31 && verb <= 36)
		   verb += _inventoryOffset;
		 */

		const byte *msg = getResourceAddress(rtVerb, verb);
		if (!msg)
			return;

		tmp = _charset->_center;
		drawString(4, msg);
		_charset->_center = tmp;

		vs->curRect.right = _charset->_str.right;
		vs->curRect.bottom = _charset->_str.bottom;
		vs->oldRect = _charset->_str;
		_charset->_str.left = _charset->_str.right;
	} else if (_game.id != GID_FT) {
		restoreVerbBG(verb);
	}
}

void ScummEngine::restoreVerbBG(int verb) {
	VerbSlot *vs;

	vs = &_verbs[verb];
	uint8 col =
		vs->bkcolor;

	if (vs->oldRect.left != -1) {
		restoreBackground(vs->oldRect, col);
		vs->oldRect.left = -1;
	}
}

void ScummEngine::drawVerbBitmap(int verb, int x, int y) {
	VerbSlot *vst = &_verbs[verb];
	VirtScreen *vs;
	bool twobufs;
	const byte *imptr = 0;
	int ydiff, xstrip;
	int imgw, imgh;
	int i, tmp;
	byte *obim;
	uint32 size;

	if ((vs = findVirtScreen(y)) == NULL)
		return;

	_gdi->disableZBuffer();

	twobufs = vs->hasTwoBuffers;
	vs->hasTwoBuffers = false;

	xstrip = x / 8;
	ydiff = y - vs->topline;

	obim = getResourceAddress(rtVerb, verb);
	assert(obim);
	if (_game.features & GF_OLD_BUNDLE) {
		imgw = obim[0];
		imgh = obim[1] / 8;
		imptr = obim + 2;
	} else if (_game.features & GF_SMALL_HEADER) {
		size = READ_LE_UINT32(obim);

		if (_game.id == GID_LOOM && _game.platform == Common::kPlatformPCEngine) {
			imgw = (*(obim + size + 10));
			imgh = (*(obim + size + 15)) / 8;
		} else {
			imgw = (*(obim + size + 11));
			imgh = (*(obim + size + 17)) / 8;
		}
		imptr = getObjectImage(obim, 1);
	} else {
		const ImageHeader *imhd = (const ImageHeader *)findResourceData(MKTAG('I','M','H','D'), obim);
		if (_game.version >= 7) {
			imgw = READ_LE_UINT16(&imhd->v7.width) / 8;
			imgh = READ_LE_UINT16(&imhd->v7.height) / 8;
		} else {
			imgw = READ_LE_UINT16(&imhd->old.width) / 8;
			imgh = READ_LE_UINT16(&imhd->old.height) / 8;
		}
		imptr = getObjectImage(obim, 1);
	}
	assert(imptr);

	if (_game.id == GID_LOOM && _game.platform == Common::kPlatformPCEngine) {
		_gdi->_distaff = (vst->verbid != 54);
	}

	for (i = 0; i < imgw; i++) {
		tmp = xstrip + i;
		_gdi->drawBitmap(imptr, vs, tmp, ydiff, imgw * 8, imgh * 8, i, 1, Gdi::dbAllowMaskOr | Gdi::dbObjectMode);
	}

	if (_game.id == GID_LOOM && _game.platform == Common::kPlatformPCEngine) {
		_gdi->_distaff = false;
	}

	vst->curRect.right = vst->curRect.left + imgw * 8;
	vst->curRect.bottom = vst->curRect.top + imgh * 8;
	vst->oldRect = vst->curRect;

	_gdi->enableZBuffer();

	vs->hasTwoBuffers = twobufs;
}

int ScummEngine::getVerbSlot(int id, int mode) const {
	int i;
	for (i = 1; i < _numVerbs; i++) {
		if (_verbs[i].verbid == id && _verbs[i].saveid == mode) {
			return i;
		}
	}
	return 0;
}

void ScummEngine::killVerb(int slot) {
	VerbSlot *vs;

	if (slot == 0)
		return;

	vs = &_verbs[slot];
	vs->verbid = 0;
	vs->curmode = 0;

	_res->nukeResource(rtVerb, slot);

	if (_game.version <= 6 && vs->saveid == 0) {
		drawVerb(slot, 0);
		verbMouseOver(0);
	}
	vs->saveid = 0;
}

void ScummEngine::setVerbObject(uint room, uint object, uint verb) {
	const byte *obimptr;
	const byte *obcdptr;
	uint32 size, size2;
	FindObjectInRoom foir;
	int i;

	if (_game.heversion >= 70) { // Windows titles. Here we always ignore room
		room = getObjectRoom(object);
	}

	if (whereIsObject(object) == WIO_FLOBJECT)
		error("Can't grab verb image from flobject");

	if (_game.features & GF_OLD_BUNDLE) {
		for (i = (_numLocalObjects-1); i > 0; i--) {
			if (_objs[i].obj_nr == object) {
				findObjectInRoom(&foir, foImageHeader, object, room);
				size = READ_LE_UINT16(foir.obim);
				byte *ptr = _res->createResource(rtVerb, verb, size + 2);
				obcdptr = getResourceAddress(rtRoom, room) + getOBCDOffs(object);
				ptr[0] = *(obcdptr + 9);	// Width
				ptr[1] = *(obcdptr + 15);	// Height
				memcpy(ptr + 2, foir.obim, size);
				return;
			}
		}
	} else if (_game.features & GF_SMALL_HEADER) {
		for (i = (_numLocalObjects-1); i > 0; i--) {
			if (_objs[i].obj_nr == object) {
				// FIXME: the only thing we need from the OBCD is the image size!
				// So we could use almost the same code (except for offsets)
				// as in the GF_OLD_BUNDLE code. But of course that would break save games
				// unless we insert special conversion code... <sigh>
				findObjectInRoom(&foir, foImageHeader, object, room);
				size = READ_LE_UINT32(foir.obim);
				obcdptr = getResourceAddress(rtRoom, room) + getOBCDOffs(object);
				size2 = READ_LE_UINT32(obcdptr);
				_res->createResource(rtVerb, verb, size + size2);
				obimptr = getResourceAddress(rtRoom, room) - foir.roomptr + foir.obim;
				obcdptr = getResourceAddress(rtRoom, room) + getOBCDOffs(object);
				memcpy(getResourceAddress(rtVerb, verb), obimptr, size);
				memcpy(getResourceAddress(rtVerb, verb) + size, obcdptr, size2);
				return;
			}
		}
	} else {
		findObjectInRoom(&foir, foImageHeader, object, room);
		size = READ_BE_UINT32(foir.obim + 4);
		_res->createResource(rtVerb, verb, size);
		obimptr = getResourceAddress(rtRoom, room) - foir.roomptr + foir.obim;
		memcpy(getResourceAddress(rtVerb, verb), obimptr, size);
	}
}

} // End of namespace Scumm
