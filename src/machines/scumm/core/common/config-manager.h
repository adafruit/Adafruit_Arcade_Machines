/* fruitjam-scumm - a fixed ConfMan for the handful of keys the SCUMM engine
 * reads. Values come from src/backend/osystem.cpp (fj_config_*), which the
 * Python adapter can set before the game starts.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef COMMON_CONFIG_MANAGER_H
#define COMMON_CONFIG_MANAGER_H

#include "str.h"

namespace Common {

class ConfigManager {
public:
	bool hasKey(const String &key) const;
	bool hasKey(const String &key, const String &domain) const { return hasKey(key); }
	String get(const String &key) const;
	int getInt(const String &key) const;
	bool getBool(const String &key) const;
	void set(const String &key, const String &value) {}
	void setInt(const String &key, int value);
	void setBool(const String &key, bool value);
	void registerDefault(const String &key, const String &value) {}
	void registerDefault(const String &key, int value) {}
	void registerDefault(const String &key, bool value) {}
	void flushToDisk() {}
};

extern ConfigManager ConfMan;

} // End of namespace Common

using Common::ConfMan;

#endif
