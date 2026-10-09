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
#include "../common/events.h"
#include "../common/system.h"
#include "../common/translation.h"
#include "../audio/mixer.h"

#include "resource.h"
#include "scumm_v3.h"
#include "sound.h"



namespace Scumm {

enum MouseButtonStatus {
	msDown = 1,
	msClicked = 2
};


void ScummEngine::parseEvent(Common::Event event) {
	switch (event.type) {
	case Common::EVENT_KEYDOWN:
		if (event.kbd.keycode >= Common::KEYCODE_0 && event.kbd.keycode <= Common::KEYCODE_9 &&
			((event.kbd.hasFlags(Common::KBD_ALT) && canSaveGameStateCurrently()) ||
			(event.kbd.hasFlags(Common::KBD_CTRL) && canLoadGameStateCurrently()))) {
			_saveLoadSlot = event.kbd.keycode - Common::KEYCODE_0;

			//  don't overwrite autosave (slot 0)
			if (_saveLoadSlot == 0)
				_saveLoadSlot = 10;

			_saveLoadDescription = Common::String::format("Quicksave %d", _saveLoadSlot);
			_saveLoadFlag = (event.kbd.hasFlags(Common::KBD_ALT)) ? 1 : 2;
			_saveTemporaryState = false;
		} else if (event.kbd.hasFlags(Common::KBD_CTRL) && event.kbd.keycode == Common::KEYCODE_f) {
			_fastMode ^= 1;
		} else if (event.kbd.hasFlags(Common::KBD_CTRL) && event.kbd.keycode == Common::KEYCODE_g) {
			_fastMode ^= 2;
		} else if (event.kbd.hasFlags(Common::KBD_CTRL) && event.kbd.keycode == Common::KEYCODE_s) {
			_res->resourceStats();
		} else if (event.kbd.hasFlags(Common::KBD_ALT) && event.kbd.keycode == Common::KEYCODE_x) {
			// TODO: Some SCUMM games quit when Alt-x is pressed. However, not
			// all of them seem to exhibit this behavior. LordHoto found that
			// the Loom manual does not mention this hotkey. On the other hand
			// the Sam&Max manual mentions that Alt-x does so on "most"
			// platforms. We should really check which games exhibit this
			// behavior and only use it for them.
			quitGame();
		} else {
			// Normal key press, pass on to the game.
			_keyPressed = event.kbd;
		}

		// FIXME: We are using ASCII values to index the _keyDownMap here,
		// yet later one code which checks _keyDownMap will use KEYCODEs
		// to do so. That is, we are mixing ascii and keycode values here,
		// which is bad. We probably should be only using keycodes, but at
		// least INSANE checks for "Shift-V" by looking for the 'V' key
		// being pressed. It would be easy to solve that by also storing
		// the modifier flags. However, since getKeyState() is also called
		// by scripts, we have to be careful with semantic changes.
		if (_keyPressed.ascii >= 512)
			debugC(DEBUG_GENERAL, "_keyPressed > 512 (%d)", _keyPressed.ascii);
		else
			_keyDownMap[_keyPressed.ascii] = true;
		break;

	case Common::EVENT_KEYUP:
		if (event.kbd.ascii >= 512) {
			debugC(DEBUG_GENERAL, "keyPressed > 512 (%d)", event.kbd.ascii);
		} else {
			_keyDownMap[event.kbd.ascii] = false;

			// Due to some weird bug with capslock key pressed
			// generated keydown event is for lower letter but
			// keyup is for upper letter
			// On most (all?) keyboards it is safe to assume that
			// both upper and lower letters are unpressed on keyup event
			//
			// Fixes bug #1709430: "FT: CAPSLOCK + V enables cheating for all fights"
			//
			// Fingolfin remarks: This wouldn't be a problem if we used keycodes.
			_keyDownMap[toupper(event.kbd.ascii)] = false;
		}
		break;


	// We update the mouse position whenever the mouse moves or a click occurs.
	// The latter is done to accomodate systems with a touchpad / pen controller.
	case Common::EVENT_LBUTTONDOWN:
	case Common::EVENT_RBUTTONDOWN:
	case Common::EVENT_MOUSEMOVE:
		if (event.type == Common::EVENT_LBUTTONDOWN)
			_leftBtnPressed |= msClicked|msDown;
		else if (event.type == Common::EVENT_RBUTTONDOWN)
			_rightBtnPressed |= msClicked|msDown;
		_mouse.x = event.mouse.x;
		_mouse.y = event.mouse.y;

		if (_renderMode == Common::kRenderHercA || _renderMode == Common::kRenderHercG) {
			_mouse.x -= (kHercWidth - _screenWidth * 2) / 2;
			_mouse.x >>= 1;
			_mouse.y = _mouse.y * 4 / 7;
		} else if (_useCJKMode && _textSurfaceMultiplier == 2) {
			_mouse.x >>= 1;
			_mouse.y >>= 1;
		}
		break;
	case Common::EVENT_LBUTTONUP:
		_leftBtnPressed &= ~msDown;
		break;

	case Common::EVENT_RBUTTONUP:
		_rightBtnPressed &= ~msDown;
		break;

	// The following two cases enable dialog choices to be scrolled
	// through in the SegaCD version of MI. Values are taken from script-14.
	// See bug report #1193185 for details.
	case Common::EVENT_WHEELDOWN:
		if (_game.id == GID_MONKEY && _game.platform == Common::kPlatformSegaCD)
			_keyPressed = Common::KeyState(Common::KEYCODE_7, 55);	// '7'
		break;

	case Common::EVENT_WHEELUP:
		if (_game.id == GID_MONKEY && _game.platform == Common::kPlatformSegaCD)
			_keyPressed = Common::KeyState(Common::KEYCODE_6, 54);	// '6'
		break;

	default:
		break;
	}
}

void ScummEngine::parseEvents() {
	Common::Event event;

	while (_eventMan->pollEvent(event)) {
		parseEvent(event);
	}
}


void ScummEngine::clearClickedStatus() {
	_keyPressed.reset();

	_mouseAndKeyboardStat = 0;
	_leftBtnPressed &= ~msClicked;
	_rightBtnPressed &= ~msClicked;
}


void ScummEngine::processInput() {
	Common::KeyState lastKeyHit = _keyPressed;
	_keyPressed.reset();

	//
	// Clip the mouse coordinates, and compute _virtualMouse.x (and clip it, too)
	//
	if (_mouse.x < 0)
		_mouse.x = 0;
	if (_mouse.x > _screenWidth-1)
		_mouse.x = _screenWidth-1;
	if (_mouse.y < 0)
		_mouse.y = 0;
	if (_mouse.y > _screenHeight-1)
		_mouse.y = _screenHeight-1;

	VirtScreen *vs = &_virtscr[kMainVirtScreen];
	_virtualMouse.x = _mouse.x + vs->xstart;
	_virtualMouse.y = _mouse.y - vs->topline;
	if (_game.version >= 7)
		_virtualMouse.y += _screenTop;

	if (_virtualMouse.y < 0)
		_virtualMouse.y = -1;
	if (_virtualMouse.y >= vs->h)
		_virtualMouse.y = -1;

	//
	// Determine the mouse button state.
	//
	_mouseAndKeyboardStat = 0;

	if ((_leftBtnPressed & msClicked) && (_rightBtnPressed & msClicked) && _game.version >= 4) {
		// Pressing both mouse buttons is treated as if you pressed
		// the cutscene exit key (ESC) in V4+ games. That mimicks
		// the behavior of the original engine where pressing both
		// mouse buttons also skips the current cutscene.
		_mouseAndKeyboardStat = 0;
		lastKeyHit = Common::KeyState(Common::KEYCODE_ESCAPE);
	} else if ((_rightBtnPressed & msClicked) && (_game.version <= 3 && _game.id != GID_LOOM)) {
		// Pressing right mouse button is treated as if you pressed
		// the cutscene exit key (ESC) in V0-V3 games. That mimicks
		// the behavior of the original engine where pressing right
		// mouse button also skips the current cutscene.
		_mouseAndKeyboardStat = 0;
		lastKeyHit = Common::KeyState(Common::KEYCODE_ESCAPE);
	} else if (_leftBtnPressed & msClicked) {
		_mouseAndKeyboardStat = MBS_LEFT_CLICK;
	} else if (_rightBtnPressed & msClicked) {
		_mouseAndKeyboardStat = MBS_RIGHT_CLICK;
	}

	if (_game.version >= 6) {
		VAR(VAR_LEFTBTN_HOLD) = (_leftBtnPressed & msDown) != 0;
		VAR(VAR_RIGHTBTN_HOLD) = (_rightBtnPressed & msDown) != 0;

		if (_game.heversion >= 72) {
			// HE72 introduced a flag for whether or not this is a click
			// or the player is continuing to hold the button down.
			// 0x80 signifies that the button is continuing to be held down
			// Backyard Soccer needs this in order to function
			if (VAR(VAR_LEFTBTN_HOLD) && !(_leftBtnPressed & msClicked))
				VAR(VAR_LEFTBTN_HOLD) |= 0x80;

			if (VAR(VAR_RIGHTBTN_HOLD) && !(_rightBtnPressed & msClicked))
				VAR(VAR_RIGHTBTN_HOLD) |= 0x80;
		} else if (_game.version >= 7) {
			VAR(VAR_LEFTBTN_DOWN) = (_leftBtnPressed & msClicked) != 0;
			VAR(VAR_RIGHTBTN_DOWN) = (_rightBtnPressed & msClicked) != 0;
		}
	}

	_leftBtnPressed &= ~msClicked;
	_rightBtnPressed &= ~msClicked;

	if (!lastKeyHit.ascii)
		return;

	processKeyboard(lastKeyHit);
}


void ScummEngine_v3::processKeyboard(Common::KeyState lastKeyHit) {
	// Fall back to default behavior
	ScummEngine::processKeyboard(lastKeyHit);

	// On Alt-F5 prepare savegame for the original save/load dialog.
	// Adafruit Arcade Machines: on plain F5 too. Desktop ScummVM keeps F5
	// for its own menu, so the game's dialog is Alt-F5 there; this port has
	// no such menu, F5 opens the game's dialog, and without the snapshot
	// its save failed ("The game was NOT saved").
	if (lastKeyHit.keycode == Common::KEYCODE_F5) {
		prepareSavegame();
	}
}

void ScummEngine::processKeyboard(Common::KeyState lastKeyHit) {
	// Enable the following five special keys conditionally:
	bool restartKeyEnabled = (VAR_RESTART_KEY == 0xFF || VAR(VAR_RESTART_KEY) != 0);
	bool pauseKeyEnabled = (VAR_PAUSE_KEY == 0xFF || VAR(VAR_PAUSE_KEY) != 0);
	bool talkstopKeyEnabled = (VAR_TALKSTOP_KEY == 0xFF || VAR(VAR_TALKSTOP_KEY) != 0);
	bool cutsceneExitKeyEnabled = (VAR_CUTSCENEEXIT_KEY == 0xFF || VAR(VAR_CUTSCENEEXIT_KEY) != 0);
	bool mainmenuKeyEnabled = (VAR_MAINMENU_KEY == 0xFF || VAR(VAR_MAINMENU_KEY) != 0);
	bool snapScrollKeyEnabled = (_game.version <= 2 || VAR_CAMERA_FAST_X != 0xFF);

	// In FM-TOWNS games F8 / restart is always enabled
	if (_game.platform == Common::kPlatformFMTowns)
		restartKeyEnabled = true;

	// For games which use VAR_MAINMENU_KEY, disable the mainmenu key if
	// requested by the scripts. We make an exception for COMI (i.e.
	// forcefully always enable it there), as that always disables it.
	if (_game.id == GID_CMI)
		mainmenuKeyEnabled = true;

	// fruitjam-scumm: there is no ScummVM main menu or restart dialog, so
	// F5 and F8 go to the game scripts like any other function key and
	// the game's own save/load screen comes up.
	if (pauseKeyEnabled && (lastKeyHit.keycode == Common::KEYCODE_SPACE && lastKeyHit.hasFlags(0))) {
		pauseGame();

	} else if (talkstopKeyEnabled && lastKeyHit.ascii == '.') {
		_talkDelay = 0;
		if (_sound->_sfxMode & 2)
			stopTalk();

	} else if (cutsceneExitKeyEnabled && (lastKeyHit.keycode == Common::KEYCODE_ESCAPE && lastKeyHit.hasFlags(0))) {
		abortCutscene();

		// VAR_CUTSCENEEXIT_KEY doesn't exist in SCUMM0
		if (VAR_CUTSCENEEXIT_KEY != 0xFF)
			_mouseAndKeyboardStat = VAR(VAR_CUTSCENEEXIT_KEY);
	} else if (snapScrollKeyEnabled && lastKeyHit.keycode == Common::KEYCODE_r &&
		lastKeyHit.hasFlags(Common::KBD_CTRL)) {
		_snapScroll ^= 1;

		if (VAR_CAMERA_FAST_X != 0xFF)
			VAR(VAR_CAMERA_FAST_X) = _snapScroll;
	} else if (lastKeyHit.ascii == '[' || lastKeyHit.ascii == ']') { // Change music volume
		int vol = ConfMan.getInt("music_volume") / 16;
		if (lastKeyHit.ascii == ']' && vol < 16)
			vol++;
		else if (lastKeyHit.ascii == '[' && vol > 0)
			vol--;

		vol *= 16;
		if (vol > Audio::Mixer::kMaxMixerVolume)
			vol = Audio::Mixer::kMaxMixerVolume;

		ConfMan.setInt("music_volume", vol);
		syncSoundSettings();

	} else if (lastKeyHit.ascii == '-' || lastKeyHit.ascii == '+') { // Change text speed
		if (lastKeyHit.ascii == '+' && _defaultTalkDelay > 0)
			_defaultTalkDelay--;
		else if (lastKeyHit.ascii == '-' && _defaultTalkDelay < 9)
			_defaultTalkDelay++;

		// Save the new talkspeed value to ConfMan
		setTalkSpeed(_defaultTalkDelay);

		if (VAR_CHARINC != 0xFF)
			VAR(VAR_CHARINC) = _defaultTalkDelay;

	} else {

		if (lastKeyHit.keycode >= Common::KEYCODE_F1 &&
		    lastKeyHit.keycode <= Common::KEYCODE_F9) {
			_mouseAndKeyboardStat = lastKeyHit.keycode - Common::KEYCODE_F1 + 315;

		} else if (_game.id == GID_MONKEY2 && (lastKeyHit.flags & Common::KBD_ALT)) {
			// Handle KBD_ALT combos in MI2. We know that the result must be 273 for Alt-W
			// because that's what MI2 looks for in its "instant win" cheat.
			_mouseAndKeyboardStat = lastKeyHit.keycode + 154;

		} else if (lastKeyHit.keycode >= Common::KEYCODE_UP &&
		          lastKeyHit.keycode <= Common::KEYCODE_LEFT) {
			if (_game.id == GID_MONKEY && _game.platform == Common::kPlatformSegaCD) {
				// Map arrow keys to number keys in the SEGA version of MI to support
				// scrolling to conversation choices. See bug report #1193185 for details.
				_mouseAndKeyboardStat = lastKeyHit.keycode - Common::KEYCODE_UP + 54;
			} else if (_game.id == GID_LOOM && _game.platform == Common::kPlatformPCEngine) {
				// Map arrow keys to number keys in the PCEngine version of Loom to support
				// the menu screen.
				switch (lastKeyHit.keycode) {
				case Common::KEYCODE_UP:
					_mouseAndKeyboardStat = 328;
					break;
				case Common::KEYCODE_DOWN:
					_mouseAndKeyboardStat = 336;
					break;
				case Common::KEYCODE_LEFT:
					_mouseAndKeyboardStat = 331;
					break;
				case Common::KEYCODE_RIGHT:
					_mouseAndKeyboardStat = 333;
					break;
				default:
					break;
				}
			} else if (_game.version >= 7) {
				// Don't let pre-V7 game see arrow keys. This fixes bug with up arrow (273)
				// corresponding to the "instant win" cheat in MI2 mentioned above.
				//
				// This is not applicable to V7+ games, which need to see the arrow keys,
				// too, else certain things (derby scene, asterorid lander) won't work.
				_mouseAndKeyboardStat = lastKeyHit.ascii;
			}

		} else {
			_mouseAndKeyboardStat = lastKeyHit.ascii;
		}
	}
}

} // End of namespace Scumm
