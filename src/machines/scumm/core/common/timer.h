/* fruitjam-scumm: timers run on the engine's virtual clock. The backend
 * fires them while the engine waits (OSystem::delayMillis), so they never
 * interrupt engine code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef COMMON_TIMER_H
#define COMMON_TIMER_H

#include "scummsys.h"

namespace Common {

class TimerManager {
public:
	typedef void (*TimerProc)(void *refCon);

	enum { kMaxTimers = 4 };

	TimerManager();
	bool installTimerProc(TimerProc proc, int32 interval, void *refCon, const char *id);
	void removeTimerProc(TimerProc proc);
	// Advance the clock by msecs and run due timers.
	void advance(uint32 msecs);

private:
	struct Slot {
		TimerProc proc;
		void *refCon;
		int32 interval; // microseconds
		int32 counter;  // microseconds until next call
	};
	Slot _slots[kMaxTimers];
};

} // End of namespace Common

#endif
