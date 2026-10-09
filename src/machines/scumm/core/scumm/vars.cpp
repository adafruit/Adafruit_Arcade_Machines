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


#include "../common/config-manager.h"
#include "scumm.h"
#include "scumm_v3.h"
#include "sound.h"

#include "../audio/mididrv.h"

namespace Scumm {

void ScummEngine::setupScummVars() {
	VAR_KEYPRESS = 0;
	VAR_EGO = 1;
	VAR_CAMERA_POS_X = 2;
	VAR_HAVE_MSG = 3;
	VAR_ROOM = 4;
	VAR_OVERRIDE = 5;
	VAR_MACHINE_SPEED = 6;
	VAR_ME = 7;
	VAR_NUM_ACTOR = 8;
	VAR_CURRENTDRIVE = 10;
	VAR_TMR_1 = 11;
	VAR_TMR_2 = 12;
	VAR_TMR_3 = 13;
	VAR_MUSIC_TIMER = 14;
	VAR_ACTOR_RANGE_MIN = 15;
	VAR_ACTOR_RANGE_MAX = 16;
	VAR_CAMERA_MIN_X = 17;
	VAR_CAMERA_MAX_X = 18;
	VAR_TIMER_NEXT = 19;
	VAR_VIRT_MOUSE_X = 20;
	VAR_VIRT_MOUSE_Y = 21;
	VAR_ROOM_RESOURCE = 22;
	VAR_LAST_SOUND = 23;
	VAR_CUTSCENEEXIT_KEY = 24;
	VAR_TALK_ACTOR = 25;
	VAR_CAMERA_FAST_X = 26;
	VAR_ENTRY_SCRIPT = 28;
	VAR_ENTRY_SCRIPT2 = 29;
	VAR_EXIT_SCRIPT = 30;
	VAR_EXIT_SCRIPT2 = 31;
	VAR_VERB_SCRIPT = 32;
	VAR_SENTENCE_SCRIPT = 33;
	VAR_INVENTORY_SCRIPT = 34;
	VAR_CUTSCENE_START_SCRIPT = 35;
	VAR_CUTSCENE_END_SCRIPT = 36;
	VAR_CHARINC = 37;
	VAR_WALKTO_OBJ = 38;
	VAR_HEAPSPACE = 40;
	VAR_RESTART_KEY = 42;
	VAR_PAUSE_KEY = 43;
	VAR_MOUSE_X = 44;
	VAR_MOUSE_Y = 45;
	VAR_TIMER = 46;
	VAR_TIMER_TOTAL = 47;
	VAR_SOUNDCARD = 48;
	VAR_VIDEOMODE = 49;

	if (_game.id == GID_LOOM && _game.platform == Common::kPlatformPCEngine) {
		VAR_MAINMENU_KEY = 50;
	}

	if (_game.version >= 4) {
		VAR_SCROLL_SCRIPT = 27;
		VAR_DEBUGMODE = 39;
		VAR_MAINMENU_KEY = 50;
		VAR_FIXEDDISK = 51;
		VAR_CURSORSTATE = 52;
		VAR_USERPUT = 53;
	}

	if (_game.version >= 5) {
		VAR_SOUNDRESULT = 56;
		VAR_TALKSTOP_KEY = 57;
		VAR_FADE_DELAY = 59;
		VAR_SOUNDPARAM = 64;
		VAR_SOUNDPARAM2 = 65;
		VAR_SOUNDPARAM3 = 66;
		VAR_INPUTMODE = 67;	// 1 is keyboard, 2 is joystick, 3 is mouse
		VAR_MEMORY_PERFORMANCE = 68;
		VAR_VIDEO_PERFORMANCE = 69;
		VAR_ROOM_FLAG = 70;
		VAR_GAME_LOADED = 71;
		VAR_NEW_ROOM = 72;
	}
}

void ScummEngine_v5::setupScummVars() {
	// Many vars are the same as in V5 & V6 games, so just call the inherited method first
	ScummEngine::setupScummVars();

	VAR_CURRENT_LIGHTS = 9;

	if (_game.version >= 4) {
		VAR_V5_TALK_STRING_Y = 54;
	}
	if ((_game.id == GID_LOOM && _game.version == 4) || _game.version >= 5) {
		VAR_NOSUBTITLES = 60;
	}
}



void ScummEngine_v5::resetScummVars() {
	ScummEngine::resetScummVars();

	if (_game.version >= 4 && _game.version <= 5)
		VAR(VAR_V5_TALK_STRING_Y) = -0x50;

	// Setup light
	VAR(VAR_CURRENT_LIGHTS) = LIGHTMODE_actor_use_base_palette | LIGHTMODE_actor_use_colors | LIGHTMODE_room_lights_on;

	if (_game.id == GID_MONKEY)
		_scummVars[74] = 1225;
}



void ScummEngine::resetScummVars() {
	if (_game.heversion < 70 && _game.version <= 6) {
		// VAR_SOUNDCARD modes
		// 0 PC Speaker
		// 1 Tandy
		// 2 CMS
		// 3 AdLib
		// 4 Roland
		switch (_sound->_musicType) {
		case MDT_NONE:
		case MDT_PCSPK:
			VAR(VAR_SOUNDCARD) = 0;
			break;
		case MDT_PCJR:
			VAR(VAR_SOUNDCARD) = 1;
			break;
		case MDT_CMS:
			VAR(VAR_SOUNDCARD) = 2;
			break;
		case MDT_ADLIB:
			VAR(VAR_SOUNDCARD) = 3;
			break;
		default:
			if ((_game.id == GID_MONKEY_EGA || _game.id == GID_MONKEY_VGA || (_game.id == GID_LOOM && _game.version == 3))
			   &&  (_game.platform == Common::kPlatformDOS)) {
				VAR(VAR_SOUNDCARD) = 4;
			} else {
				VAR(VAR_SOUNDCARD) = 3;
			}
			break;
		}

		if (_game.platform == Common::kPlatformFMTowns)
			VAR(VAR_VIDEOMODE) = 42;
		// Value only used by the Macintosh version of Indiana Jones and the Last Crusade
		else if (_game.platform == Common::kPlatformMacintosh && _game.version == 3)
			VAR(VAR_VIDEOMODE) = 50;
		// Value only used by the Amiga version of Monkey Island 2
		else if (_game.platform == Common::kPlatformAmiga)
			VAR(VAR_VIDEOMODE) = 82;
		else if (_renderMode == Common::kRenderCGA)
			VAR(VAR_VIDEOMODE) = 4;
		else if (_renderMode == Common::kRenderHercA || _renderMode == Common::kRenderHercG)
			VAR(VAR_VIDEOMODE) = 30;
		else if (_renderMode == Common::kRenderEGA)
			VAR(VAR_VIDEOMODE) = 13;
		else
			VAR(VAR_VIDEOMODE) = 19;

		if (_game.platform == Common::kPlatformMacintosh && (_game.features & GF_OLD_BUNDLE)) {
			// Set screen size for the Macintosh version of Indy3/Loom
			VAR(39) = 320;
		}
		if (_game.platform == Common::kPlatformDOS && _game.id == GID_LOOM && _game.version == 3) {
			// Set number of sound resources
			VAR(39) = 80;
		}

		if (_game.id == GID_LOOM || _game.version >= 4)
			VAR(VAR_HEAPSPACE) = 1400;
		if (_game.version >= 4)
			VAR(VAR_FIXEDDISK) = 1;
		if (_game.version >= 5)
			VAR(VAR_INPUTMODE) = 3;
		if (_game.version == 6)
			VAR(VAR_V6_EMSSPACE) = 10000;

		if (_game.heversion >= 60) {
			// Set fast speed, to enable all animations
			VAR(VAR_MACHINE_SPEED) = 2;

			VAR(VAR_SOUNDPARAM) = 1;  // Soundblaster for music
			VAR(VAR_SOUNDPARAM2) = 1; // Soundblaster for sound effects
		}
	}

	if (VAR_ROOM_WIDTH != 0xFF && VAR_ROOM_HEIGHT != 0xFF) {
		VAR(VAR_ROOM_WIDTH) = _screenWidth;
		VAR(VAR_ROOM_HEIGHT) = _screenHeight;
	}

	if (VAR_DEBUGMODE != 0xFF) {
		VAR(VAR_DEBUGMODE) = (_debugMode ? 1 : 0);
		if (_game.heversion >= 80 && _debugMode)
			VAR(85) = 1;
	}

	if (VAR_FADE_DELAY != 0xFF)
		VAR(VAR_FADE_DELAY) = 3;

	VAR(VAR_CHARINC) = 4;
	setTalkingActor(0);
}

} // End of namespace Scumm
