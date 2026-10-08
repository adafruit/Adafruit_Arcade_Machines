/* fruitjam-scumm: the engine runs on one thread (a coroutine strictly
 * alternating with the host), so ScummVM's mutexes become no-ops.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef COMMON_MUTEX_H
#define COMMON_MUTEX_H

#include "scummsys.h"

namespace Common {

class Mutex {
public:
	void lock() {}
	void unlock() {}
};

class StackLock {
public:
	StackLock(Mutex &mutex, const char *mutexName = NULL) {}
	StackLock(const Mutex &mutex, const char *mutexName = NULL) {}
};

} // End of namespace Common

#endif
