/* fruitjam-scumm: debug channels are never enabled. */
#ifndef COMMON_DEBUG_CHANNELS_H
#define COMMON_DEBUG_CHANNELS_H

#include "scummsys.h"

namespace Common {

class DebugManager {
public:
	bool addDebugChannel(uint32 channel, const char *name, const char *description) { return true; }
	void clearAllDebugChannels() {}
	bool isDebugChannelEnabled(uint32 channel) { return false; }
};

extern DebugManager DebugMan;

} // End of namespace Common

using Common::DebugMan;

#endif
