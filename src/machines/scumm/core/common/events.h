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

// fruitjam-scumm: the Event struct and EventType enum are ScummVM's; the
// EventManager below is a small fixed queue fed by src/backend.

#ifndef COMMON_EVENTS_H
#define COMMON_EVENTS_H

#include "keyboard.h"
#include "rect.h"

namespace Common {

enum EventType {
	EVENT_INVALID = 0,
	/** A key was pressed, details in Event::kbd. */
	EVENT_KEYDOWN = 1,
	/** A key was released, details in Event::kbd. */
	EVENT_KEYUP = 2,
	/** The mouse moved, details in Event::mouse. */
	EVENT_MOUSEMOVE = 3,
	EVENT_LBUTTONDOWN = 4,
	EVENT_LBUTTONUP = 5,
	EVENT_RBUTTONDOWN = 6,
	EVENT_RBUTTONUP = 7,
	EVENT_WHEELUP = 8,
	EVENT_WHEELDOWN = 9,
	EVENT_MBUTTONDOWN = 13,
	EVENT_MBUTTONUP = 14,

	EVENT_MAINMENU = 15,
	EVENT_RETURN_TO_LAUNCHER = 16,
	EVENT_MUTE = 17,

	EVENT_QUIT = 10,
	EVENT_SCREEN_CHANGED = 11,

	/** The input devices have changed, input related configuration needs to be re-applied */
	EVENT_INPUT_CHANGED  = 35,

	/**
	 * The backend requests the agi engine's predictive dialog to be shown.
	 * TODO: Fingolfin suggests that it would be of better value to expand
	 * on this notion by generalizing its use. For example the backend could
	 * use events to ask for the save game dialog or to pause the engine.
	 * An associated enumerated type can accomplish this.
	 **/
	EVENT_PREDICTIVE_DIALOG = 12,

	EVENT_CUSTOM_BACKEND_ACTION_START = 18,
	EVENT_CUSTOM_BACKEND_ACTION_END   = 19,
	EVENT_CUSTOM_BACKEND_ACTION_AXIS  = 34,
	EVENT_CUSTOM_ENGINE_ACTION_START  = 20,
	EVENT_CUSTOM_ENGINE_ACTION_END    = 21,

#ifdef ENABLE_VKEYBD
	EVENT_VIRTUAL_KEYBOARD = 22,
#endif

	EVENT_DROP_FILE = 23,

	EVENT_JOYAXIS_MOTION = 24,
	EVENT_JOYBUTTON_DOWN = 25,
	EVENT_JOYBUTTON_UP = 26,

	EVENT_CLIPBOARD_UPDATE = 27,

	EVENT_CUSTOM_BACKEND_HARDWARE = 28,
	EVENT_DEBUGGER = 29,

	/**
	 * Additional mouse events, details in Event::mouse.
	 *
	 * Note that X1 and X2 are usually back and forward, however
	 * this can't be guaranteed on all platforms.
	 */
	EVENT_X1BUTTONDOWN = 30,
	EVENT_X1BUTTONUP = 31,
	EVENT_X2BUTTONDOWN = 32,
	EVENT_X2BUTTONUP = 33,

	/** ScummVM has gained or lost focus */
	EVENT_FOCUS_GAINED = 36,
	EVENT_FOCUS_LOST = 37
};

const int16 JOYAXIS_MIN = -32768;
const int16 JOYAXIS_MAX = 32767;

/**
 * Data structure for joystick events
 */
struct JoystickState {
	/** The axis for EVENT_JOYAXIS_MOTION events */
	byte axis;
	/** The new axis position for EVENT_JOYAXIS_MOTION events */
	int16 position;
	/**
	 * The button index for EVENT_JOYBUTTON_DOWN/UP events
	 *
	 * Some of the button indices match well-known game controller
	 * buttons. See JoystickButton.
	 */
	uint8 button;

	JoystickState() : axis(0), position(0), button(0) {}
};

/**
 *  The list of named buttons available from a joystick
 */
enum JoystickButton {
	JOYSTICK_BUTTON_A,
	JOYSTICK_BUTTON_B,
	JOYSTICK_BUTTON_X,
	JOYSTICK_BUTTON_Y,
	JOYSTICK_BUTTON_BACK,
	JOYSTICK_BUTTON_GUIDE,
	JOYSTICK_BUTTON_START,
	JOYSTICK_BUTTON_LEFT_STICK,
	JOYSTICK_BUTTON_RIGHT_STICK,
	JOYSTICK_BUTTON_LEFT_SHOULDER,
	JOYSTICK_BUTTON_RIGHT_SHOULDER,
	JOYSTICK_BUTTON_DPAD_UP,
	JOYSTICK_BUTTON_DPAD_DOWN,
	JOYSTICK_BUTTON_DPAD_LEFT,
	JOYSTICK_BUTTON_DPAD_RIGHT
};

/**
 *  The list of named axes available from a joystick
 */
enum JoystickAxis {
	JOYSTICK_AXIS_LEFT_STICK_X,
	JOYSTICK_AXIS_LEFT_STICK_Y,
	JOYSTICK_AXIS_RIGHT_STICK_X,
	JOYSTICK_AXIS_RIGHT_STICK_Y,
	JOYSTICK_AXIS_LEFT_TRIGGER,
	JOYSTICK_AXIS_RIGHT_TRIGGER
};

/**
 *  The list named buttons available from a mouse
 */
enum MouseButton {
	MOUSE_BUTTON_LEFT   = 0,
	MOUSE_BUTTON_RIGHT  = 1,
	MOUSE_BUTTON_MIDDLE = 2,
	MOUSE_WHEEL_UP      = 3,
	MOUSE_WHEEL_DOWN    = 4,
	MOUSE_BUTTON_X1     = 5,
	MOUSE_BUTTON_X2     = 6
};

typedef uint32 CustomEventType;

/**
 * Data structure for an event. A pointer to an instance of Event
 * can be passed to pollEvent.
 */
struct Event {

	/** The type of the event. */
	EventType type;

	/**
	 * True if this is a key down repeat event.
	 *
	 * Only valid for EVENT_KEYDOWN events.
	 */
	bool kbdRepeat;

	/**
	  * Keyboard data; only valid for keyboard events (EVENT_KEYDOWN and
	  * EVENT_KEYUP). For all other event types, content is undefined.
	  */
	KeyState kbd;

	/**
	 * The mouse coordinates, in virtual screen coordinates. Only valid
	 * for mouse events.
	 * Virtual screen coordinates means: the coordinate system of the
	 * screen area as defined by the most recent call to initSize().
	 */
	Point mouse;

	CustomEventType customType;


	/**
	 * Joystick data; only valid for joystick events (EVENT_JOYAXIS_MOTION,
	 * EVENT_JOYBUTTON_DOWN and EVENT_JOYBUTTON_UP).
	 */
	JoystickState joystick;

	Event() : type(EVENT_INVALID), kbdRepeat(false), customType(0) {
	}
};

class EventManager {
public:
	enum { kQueueSize = 32 };

	EventManager() : _head(0), _tail(0), _buttonState(0) {}

	bool pollEvent(Event &event);
	void pushEvent(const Event &event);
	Point getMousePos() const { return _mousePos; }
	int getButtonState() const { return _buttonState; }
	int getModifierState() const { return 0; }
	int shouldQuit() const { return 0; }
	int shouldReturnToLauncher() const { return 0; }
	void resetQuit() {}
	void resetReturnToLauncher() {}

	enum {
		LBUTTON = 1 << 0,
		RBUTTON = 1 << 1
	};

private:
	Event _queue[kQueueSize];
	int _head, _tail;
	Point _mousePos;
	int _buttonState;
};

} // End of namespace Common

#endif
