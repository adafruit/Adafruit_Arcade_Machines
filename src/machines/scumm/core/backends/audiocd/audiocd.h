/* fruitjam-scumm: the AudioCDManager interface from ScummVM. The backend's
 * version never finds a CD or ripped tracks; Loom Steam/GOG audio goes
 * through CDDA.SOU and the mixer instead.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef BACKENDS_AUDIOCD_ABSTRACT_H
#define BACKENDS_AUDIOCD_ABSTRACT_H

#include "../../audio/mixer.h"
#include "../../common/scummsys.h"

class AudioCDManager {
public:
	struct Status {
		bool playing;
		int track;
		int start;
		int duration;
		int numLoops;
		int volume;
		int balance;
	};

	bool open() { return false; }
	void close() {}
	bool play(int track, int numLoops, int startFrame, int duration, bool onlyEmulate = false,
		Audio::Mixer::SoundType soundType = Audio::Mixer::kMusicSoundType) { return false; }
	bool isPlaying() const { return false; }
	void setVolume(byte volume) {}
	void setBalance(int8 balance) {}
	void stop() {}
	void update() {}
	Status getStatus() const { Status s = { false, 0, 0, 0, 0, 0, 0 }; return s; }
};

#endif
