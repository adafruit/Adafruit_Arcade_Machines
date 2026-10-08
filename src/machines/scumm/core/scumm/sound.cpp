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
#include "../common/util.h"
#include "../common/ptr.h"
#include "../common/substream.h"

#include "actor.h"
#include "cdda.h"
#include "fj_cdtrack.h"
#include "file.h"
#include "resource.h"
#include "scumm.h"
#include "scumm_v3.h"
#include "sound.h"
#include "music.h"

#include "../common/timer.h"

#include "../audio/audiostream.h"
#include "../audio/timestamp.h"
#include "../audio/mididrv.h"
#include "../audio/mixer.h"
#include "../audio/decoders/raw.h"

namespace Scumm {

struct MP3OffsetTable {					/* Compressed Sound (.SO3) */
	int org_offset;
	int new_offset;
	int num_tags;
	int compressed_size;
};


Sound::Sound(ScummEngine *parent, Audio::Mixer *mixer)
	:
	_vm(parent),
	_mixer(mixer),
	_soundQuePos(0),
	_soundQue2Pos(0),
	_sfxFilename(),
	_sfxFileEncByte(0),
	_offsetTable(0),
	_numSoundEffects(0),
	_soundMode(kVOCMode),
	_talk_sound_a1(0),
	_talk_sound_a2(0),
	_talk_sound_b1(0),
	_talk_sound_b2(0),
	_talk_sound_mode(0),
	_talk_sound_channel(0),
	_mouthSyncMode(false),
	_endOfMouthSync(false),
	_curSoundPos(0),
	_currentCDSound(0),
	_currentMusic(0),
	_lastSound(0),
	_soundsPaused(false),
	_sfxMode(0) {

	memset(_soundQue, 0, sizeof(_soundQue));
	memset(_soundQue2, 0, sizeof(_soundQue2));
	memset(_mouthSyncTimes, 0, sizeof(_mouthSyncTimes));

	_musicType = MDT_NONE;

	_loomSteamCD.playing = false;
	_loomSteamCD.track = 0;
	_loomSteamCD.start = 0;
	_loomSteamCD.duration = 0;
	_loomSteamCD.numLoops = 0;
	_loomSteamCD.volume = Audio::Mixer::kMaxChannelVolume;
	_loomSteamCD.balance = 0;

	_isLoomSteam = _vm->_game.id == GID_LOOM && Common::File::exists("CDDA.SOU");
	_isLoomPCE = _vm->_game.id == GID_LOOM && _vm->_game.platform == Common::kPlatformPCEngine;

	_loomSteamCDAudioHandle = new Audio::SoundHandle();
	_talkChannelHandle = new Audio::SoundHandle();
}

Sound::~Sound() {
	stopCDTimer();
	stopCD();
	free(_offsetTable);
	delete _loomSteamCDAudioHandle;
	delete _talkChannelHandle;
}

void Sound::addSoundToQueue(int sound, int heOffset, int heChannel, int heFlags, int heFreq, int hePan, int heVol) {
	if (_vm->VAR_LAST_SOUND != 0xFF)
		_vm->VAR(_vm->VAR_LAST_SOUND) = sound;
	_lastSound = sound;

	// HE music resources are in separate file
	if (sound <= _vm->_numSounds)
		_vm->ensureResourceLoaded(rtSound, sound);

	addSoundToQueue2(sound, heOffset, heChannel, heFlags, heFreq, hePan, heVol);
}

void Sound::addSoundToQueue2(int sound, int heOffset, int heChannel, int heFlags, int heFreq, int hePan, int heVol) {
	assert(_soundQue2Pos < ARRAYSIZE(_soundQue2));
	_soundQue2[_soundQue2Pos].sound = sound;
	_soundQue2[_soundQue2Pos].offset = heOffset;
	_soundQue2[_soundQue2Pos].channel = heChannel;
	_soundQue2[_soundQue2Pos].flags = heFlags;
	_soundQue2[_soundQue2Pos].freq = heFreq;
	_soundQue2[_soundQue2Pos].pan = hePan;
	_soundQue2[_soundQue2Pos].vol = heVol;
	_soundQue2Pos++;
}

void Sound::processSound() {
	if (_vm->_game.version >= 7) {
		processSfxQueues();
	} else if (_vm->_game.heversion >= 80) {
		processSoundQueues();
	} else {
		processSfxQueues();
		processSoundQueues();
	}
}

void Sound::processSoundQueues() {
	int i = 0, num;
	int snd;
	int data[16];

	while (_soundQue2Pos) {
		_soundQue2Pos--;
		snd = _soundQue2[_soundQue2Pos].sound;
		if (snd)
			playSound(snd);
	}

	while (i < _soundQuePos) {
		num = _soundQue[i++];
		if (i + num > _soundQuePos) {
			error("processSoundQues: invalid num value");
			break;
		}
		memset(data, 0, sizeof(data));
		if (num > 0) {
			for (int j = 0; j < num; j++)
				data[j] = _soundQue[i + j];
			i += num;

			debugC(DEBUG_IMUSE, "processSoundQues(%d,%d,%d,%d,%d,%d,%d,%d,%d)",
						data[0] >> 8, data[0] & 0xFF,
						data[1], data[2], data[3], data[4], data[5], data[6], data[7]);

			// fruitjam-scumm: no iMuse or FM-Towns player to send commands to
		}
	}
	_soundQuePos = 0;
}

void Sound::playSound(int soundID) {
	byte *ptr;
	byte *sound;
	Audio::AudioStream *stream;
	int size = -1;
	int rate;

	if (_vm->_game.id == GID_LOOM && _vm->_game.platform == Common::kPlatformPCEngine) {
		if (soundID >= 13 && soundID <= 32) {
			static const char tracks[20] = {3, 4, 5, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 19, 20, 21};

			_currentCDSound = soundID;

			// The original game had hard-coded lengths for all
			// tracks, but this one track is the only one (as far
			// as we know) where this actually matters. See bug
			// #3024173 - LOOM-PCE: Music stops prematurely.

			int track = tracks[soundID - 13];
			if (track == 6) {
				playCDTrack(track, 1, 0, 260);
			} else {
				playCDTrack(track, 1, 0, 0);
			}
		} else {
			if (_vm->_musicEngine) {
				_vm->_musicEngine->startSound(soundID);
			}
		}
		return;
	}

	debugC(DEBUG_SOUND, "playSound #%d", soundID);

	ptr = _vm->getResourceAddress(rtSound, soundID);

	if (!ptr) {
		return;
	}

	// Support for SFX in Monkey Island 1, Mac version
	// This is rather hackish right now, but works OK. SFX are not sounding
	// 100% correct, though, not sure right now what is causing this.
	else if (READ_BE_UINT32(ptr) == MKTAG('M','a','c','1')) {
		// Read info from the header
		size = READ_BE_UINT32(ptr+0x60);
		rate = READ_BE_UINT16(ptr+0x64);

		// Skip over the header (fixed size)
		ptr += 0x72;

		// Allocate a sound buffer, copy the data into it, and play
		sound = (byte *)malloc(size);
		memcpy(sound, ptr, size);

		stream = Audio::makeRawStream(sound, size, rate, Audio::FLAG_UNSIGNED);
		_mixer->playStream(Audio::Mixer::kSFXSoundType, NULL, stream, soundID);
	}
	// WORKAROUND bug # 1311447
	else if (READ_BE_UINT32(ptr) == 0x460e200d) {
		// This sound resource occurs in the Macintosh version of Monkey Island.
		// I do now know whether it is used in any place other than the one
		// mentioned in the bug report above; in case it is, I put a check here.
		assert(soundID == 39);

		// The samplerate is copied from the sound resource 39 of the PC CD/VGA
		// version of Monkey Island.

		// Read info from the header
		size = READ_BE_UINT32(ptr+4);
		rate = 6849;

		// Skip over the header (fixed size)
		ptr += 0x26;

		// Allocate a sound buffer, copy the data into it, and play
		sound = (byte *)malloc(size);
		memcpy(sound, ptr, size);
		stream = Audio::makeRawStream(sound, size, rate, Audio::FLAG_UNSIGNED);
		_mixer->playStream(Audio::Mixer::kSFXSoundType, NULL, stream, soundID);
	}
	else if (_vm->_game.platform != Common::kPlatformFMTowns && READ_BE_UINT32(ptr) == MKTAG('S','O','U','N')) {
		if (_vm->_game.version != 3)
			ptr += 2;

		int type = *(ptr + 0x0D);

		if (type == 2) {
			// CD track resource
			ptr += 0x16;
			if (soundID == _currentCDSound && pollCD() == 1)
				return;

			int track = ptr[0];
			int loops = ptr[1];
			int start = (ptr[2] * 60 + ptr[3]) * 75 + ptr[4];
			int end = (ptr[5] * 60 + ptr[6]) * 75 + ptr[7];

			playCDTrack(track, loops == 0xff ? -1 : loops, start, end <= start ? 0 : end - start);
			_currentCDSound = soundID;
		} else {
			// All other sound types are ignored
			warning("Scumm::Sound::playSound: encountered audio resource with chunk type 'SOUN' and sound type %d", type);
		}
	}
	else if ((_vm->_game.platform == Common::kPlatformMacintosh) && (_vm->_game.id == GID_INDY3) && READ_BE_UINT16(ptr + 8) == 0x1C) {
		// Sound format as used in Indy3 EGA Mac.
		// It seems to be closely related to the Amiga format, see player_v3a.cpp
		// The following is known:
		// offset 0, 16 LE: total size
		// offset 2-7: ?
		// offset 8, 16BE: offset to sound data (0x1C = 28 -> header size 28?)
		// offset 10-11: ? another offset, maybe related to looping?
		// offset 12, 16BE: size of sound data
		// offset 14-15: ? often the same as 12-13: maybe loop size/end?
		// offset 16-19: ? all 0?
		// offset 20, 16BE: rate divisor
		// offset 22-23: ? often identical to the rate divisior? (but not in sound 8, which loops)
		// offset 24, byte (?): volume
		// offset 25: ? same as volume -- maybe left vs. right channel?
		// offset 26: ?  if != 0: stop current sound?
		// offset 27: ?  loopcount? 0xff == -1 for infinite?

		size = READ_BE_UINT16(ptr + 12);
		assert(size);

		rate = 3579545 / READ_BE_UINT16(ptr + 20);
		sound = (byte *)malloc(size);
		int vol = ptr[24] * 4;
		int loopStart = 0, loopEnd = 0;
		int loopcount = ptr[27];

		memcpy(sound, ptr + READ_BE_UINT16(ptr + 8), size);
		Audio::SeekableAudioStream *plainStream = Audio::makeRawStream(sound, size, rate, 0);

		if (loopcount > 1) {
			loopStart = READ_BE_UINT16(ptr + 10) - READ_BE_UINT16(ptr + 8);
			loopEnd = READ_BE_UINT16(ptr + 14);

			// TODO: Currently we will only ever play till "loopEnd", even when we only have
			// a finite repetition count.
			stream = new Audio::SubLoopingAudioStream(plainStream, loopcount == 255 ? 0 : loopcount, Audio::Timestamp(0, loopStart, rate), Audio::Timestamp(0, loopEnd, rate));
		} else {
			stream = plainStream;
		}

		_mixer->playStream(Audio::Mixer::kSFXSoundType, NULL, stream, soundID, vol, 0);
	}
	else {

		if (_vm->_musicEngine) {
			_vm->_musicEngine->startSound(soundID);
		}
	}
}

void Sound::processSfxQueues() {

	if (_talk_sound_mode != 0) {
		if (_talk_sound_mode & 1)
			startTalkSound(_talk_sound_a1, _talk_sound_b1, 1);
		if (_talk_sound_mode & 2)
			startTalkSound(_talk_sound_a2, _talk_sound_b2, 2, _talkChannelHandle);
		_talk_sound_mode = 0;
	}

	const int act = _vm->getTalkingActor();
	if ((_sfxMode & 2) && act != 0) {
		Actor *a;
		bool finished;

		if (_vm->_imuseDigital) {
			finished = !isSoundRunning(kTalkSoundID);
		} else if (_vm->_game.heversion >= 60) {
			finished = !isSoundRunning(1);
		} else {
			finished = !_mixer->isSoundHandleActive(*_talkChannelHandle);
			// calculate speech sound position simulating increment at 60FPS
			_curSoundPos = (_mixer->getSoundElapsedTime(*_talkChannelHandle) * 60) / 1000;
		}
		if ((uint) act < 0x80 && ((_vm->_game.version == 8) || (_vm->_game.version <= 7 && !_vm->_string[0].no_talk_anim))) {
			a = _vm->derefActor(act, "processSfxQueues");
			if (a->isInCurrentRoom()) {
				if (finished || (isMouthSyncOff(_curSoundPos) && _mouthSyncMode)) {
					a->runActorTalkScript(a->_talkStopFrame);
					_mouthSyncMode = 0;
				} else if (isMouthSyncOff(_curSoundPos) == 0 && !_mouthSyncMode) {
					a->runActorTalkScript(a->_talkStartFrame);
					_mouthSyncMode = 1;
				}
			}
		}

		if ((!ConfMan.getBool("subtitles") && finished) || (finished && _vm->_talkDelay == 0)) {
			if (!(_vm->_game.version == 8 && _vm->VAR(_vm->VAR_HAVE_MSG) == 0))
				_vm->stopTalk();
		}
	}

	if (_sfxMode & 1) {
		if (isSfxFinished()) {
			_sfxMode &= ~1;
		}
	}
}

void Sound::startTalkSound(uint32 offset, uint32 b, int mode, Audio::SoundHandle *handle) {
	// fruitjam-scumm: no MONSTER.SOU / VOC speech. Loom CD speech is CD audio.
	_sfxMode |= mode;
}

void Sound::stopTalkSound() {
	if (_sfxMode & 2) {
		if (_vm->_imuseDigital) {
		} else if (_vm->_game.heversion >= 60) {
			stopSound(1);
		} else {
			_mixer->stopHandle(*_talkChannelHandle);
		}
		_sfxMode &= ~2;
	}
}

bool Sound::isMouthSyncOff(uint pos) {
	uint j;
	bool val = true;
	uint16 *ms = _mouthSyncTimes;

	_endOfMouthSync = false;
	do {
		val = !val;
		j = *ms++;
		if (j == 0xFFFF) {
			_endOfMouthSync = true;
			break;
		}
	} while (pos > j);
	return val;
}

int Sound::isSoundRunning(int sound) const {

	if (sound == _currentCDSound)
		return pollCD();

	if (_mixer->isSoundIDActive(sound))
		return 1;

	if (isSoundInQueue(sound))
		return 1;

	if (sound > _vm->_numSounds || !_vm->_res->isResourceLoaded(rtSound, sound))
		return 0;

	if (_vm->_musicEngine)
		return _vm->_musicEngine->getSoundStatus(sound);

	return 0;
}

/**
 * Check whether the sound resource with the specified ID is still
 * used. This is invoked by ScummEngine::isResourceInUse, to determine
 * which resources can be expired from memory.
 * Technically, this works very similar to isSoundRunning, however it
 * calls IMuse::get_sound_active() instead of IMuse::getSoundStatus().
 * The difference between those two is in how they treat sounds which
 * are being faded out: get_sound_active() returns true even when the
 * sound is being faded out, while getSoundStatus() returns false in
 * that case.
 */
bool Sound::isSoundInUse(int sound) const {


	if (sound == _currentCDSound)
		return pollCD() != 0;

	if (isSoundInQueue(sound))
		return true;

	if (!_vm->_res->isResourceLoaded(rtSound, sound))
		return false;

	if (_mixer->isSoundIDActive(sound))
		return 1;

	return false;
}

bool Sound::isSoundInQueue(int sound) const {
	int i, num;

	i = _soundQue2Pos;
	while (i--) {
		if (_soundQue2[i].sound == sound)
			return true;
	}

	i = 0;
	while (i < _soundQuePos) {
		num = _soundQue[i++];

		if (num > 0) {
			if (_soundQue[i + 0] == 0x10F && _soundQue[i + 1] == 8 && _soundQue[i + 2] == sound)
				return true;
			i += num;
		}
	}
	return false;
}

void Sound::stopSound(int sound) {
	int i;

	if (sound != 0 && sound == _currentCDSound) {
		_currentCDSound = 0;
		stopCD();
		stopCDTimer();
	}

	if (_vm->_game.version < 7)
		_mixer->stopID(sound);

	if (_vm->_musicEngine)
		_vm->_musicEngine->stopSound(sound);

	for (i = 0; i < ARRAYSIZE(_soundQue2); i++) {
		if (_soundQue2[i].sound == sound) {
			_soundQue2[i].sound = 0;
			_soundQue2[i].offset = 0;
			_soundQue2[i].channel = 0;
			_soundQue2[i].flags = 0;
			_soundQue2[i].freq = 0;
			_soundQue2[i].pan = 0;
			_soundQue2[i].vol = 0;
		}
	}
}

void Sound::stopAllSounds() {
	if (_currentCDSound != 0) {
		_currentCDSound = 0;
		stopCD();
		stopCDTimer();
	}

	// Clear the (secondary) sound queue
	_lastSound = 0;
	_soundQue2Pos = 0;
	memset(_soundQue2, 0, sizeof(_soundQue2));

	if (_vm->_musicEngine) {
		_vm->_musicEngine->stopAllSounds();
	}

	// Stop all SFX
	if (!_vm->_imuseDigital) {
		_mixer->stopAll();
	}
}

void Sound::soundKludge(int *list, int num) {
	int i;


	if (list[0] == -1) {
		processSound();
	} else {
		_soundQue[_soundQuePos++] = num;

		for (i = 0; i < num; i++) {
			_soundQue[_soundQuePos++] = list[i];
		}
	}
}

void Sound::talkSound(uint32 a, uint32 b, int mode, int channel) {
	if (mode == 1) {
		_talk_sound_a1 = a;
		_talk_sound_b1 = b;
		_talk_sound_channel = channel;
	} else {
		_talk_sound_a2 = a;
		_talk_sound_b2 = b;
	}

	_talk_sound_mode |= mode;
}

void Sound::setupSound() {
	setupSfxFile();

	if (_vm->_game.id == GID_FT) {
		_vm->VAR(_vm->VAR_VOICE_BUNDLE_LOADED) = _sfxFilename.empty() ? 0 : 1;
	}
}

void Sound::pauseSounds(bool pause) {
	// Don't pause sounds if the game isn't active
	// FIXME - this is quite a nasty hack, replace with something cleaner, and w/o
	// having to access member vars directly!
	if (!_vm->_roomResource)
		return;

	_soundsPaused = pause;


	_mixer->pauseAll(pause);

	if ((_vm->_game.features & GF_AUDIOTRACKS) && _vm->VAR(_vm->VAR_MUSIC_TIMER) > 0) {
		if (pause)
			stopCDTimer();
		else
			startCDTimer();
	}
}

void Sound::setupSfxFile() {
	// fruitjam-scumm: no separate speech file support
	_offsetTable = NULL;
	_sfxFileEncByte = 0;
	_sfxFilename.clear();
}

bool Sound::isSfxFinished() const {
	return !_mixer->hasActiveChannelOfType(Audio::Mixer::kSFXSoundType);
}

// We use a real timer in an attempt to get better sync with CD tracks. This is
// necessary for games like Loom CD.

static void cd_timer_handler(void *refCon) {
	ScummEngine *scumm = (ScummEngine *)refCon;

	// FIXME: Turn off the timer when it's no longer needed. In theory, it
	// should be possible to check with pollCD(), but since CD sound isn't
	// properly restarted when reloading a saved game, I don't dare to.

	scumm->VAR(scumm->VAR_MUSIC_TIMER) += 6;
}

void Sound::startCDTimer() {
	// This timer interval is based on two scenes: The Monkey Island 1
	// intro, and the scene in Loom CD where Chaos appears. In both cases
	// the game plays the scene as two separate sounds, even though both
	// halves are right next to each other in the CD track. Probably so
	// that you can hit Escape to skip the first half.
	//
	// Make it too low, and the Monkey Island theme will be cut short. Make
	// it too high, and there will be a nasty "hiccup" just as Chaos
	// appears.

	g_system->getTimerManager()->removeTimerProc(&cd_timer_handler);
	g_system->getTimerManager()->installTimerProc(&cd_timer_handler, 100700, _vm, "scummCDtimer");
}

void Sound::stopCDTimer() {
	g_system->getTimerManager()->removeTimerProc(&cd_timer_handler);
}

void Sound::playCDTrack(int track, int numLoops, int startFrame, int duration) {
	// Reset the music timer variable at the start of a new track
	_vm->VAR(_vm->VAR_MUSIC_TIMER) = 0;

	// Play it
	if (!_soundsPaused)
		playCDTrackInternal(track, numLoops, startFrame, duration);

	// Start the timer after starting the track. Starting an MP3 track is
	// almost instantaneous, but a CD player may take some time. Hopefully
	// playCD() will block during that delay.
	startCDTimer();
}

void Sound::playCDTrackInternal(int track, int numLoops, int startFrame, int duration) {
	_loomSteamCD.track = track;
	_loomSteamCD.numLoops = numLoops;
	_loomSteamCD.start = startFrame;
	_loomSteamCD.duration = duration;

	if (_isLoomPCE) {
		// fruitjam-scumm: the PC Engine CD's audio tracks, converted to
		// IMA ADPCM WAV. The game only plays whole tracks (startFrame 0),
		// once. A missing file plays nothing.
		_mixer->stopHandle(*_loomSteamCDAudioHandle);
		Common::File *f = new Common::File();
		char name[16];
		snprintf(name, sizeof(name), "Track%02d.wav", track);
		if (f->open(name)) {
			Audio::AudioStream *stream = makeFjCDTrackStream(f, duration);
			if (stream)
				_mixer->playStream(Audio::Mixer::kMusicSoundType, _loomSteamCDAudioHandle, stream);
		} else {
			delete f;
		}
	} else if (!_isLoomSteam) {
		g_system->getAudioCDManager()->play(track, numLoops, startFrame, duration);
	} else {
		// Stop any currently playing track
		_mixer->stopHandle(*_loomSteamCDAudioHandle);

		Common::File *cddaFile = new Common::File();
		if (cddaFile->open("CDDA.SOU")) {
			Audio::Timestamp start = Audio::Timestamp(0, startFrame, 75);
			Audio::Timestamp end = Audio::Timestamp(0, startFrame + duration, 75);
			Audio::SeekableAudioStream *stream = makeCDDAStream(cddaFile, DisposeAfterUse::YES);

			_mixer->playStream(Audio::Mixer::kMusicSoundType, _loomSteamCDAudioHandle,
			                    Audio::makeLoopingAudioStream(stream, start, end, (numLoops < 1) ? numLoops + 1 : numLoops));
		} else {
			delete cddaFile;
		}
	}
}

void Sound::stopCD() {
	if (!_isLoomSteam && !_isLoomPCE)
		g_system->getAudioCDManager()->stop();
	else
		_mixer->stopHandle(*_loomSteamCDAudioHandle);
}

int Sound::pollCD() const {
	if (!_isLoomSteam && !_isLoomPCE)
		return g_system->getAudioCDManager()->isPlaying();
	else
		return _mixer->isSoundHandleActive(*_loomSteamCDAudioHandle);
}

void Sound::updateCD() {
	if (!_isLoomSteam && !_isLoomPCE)
		g_system->getAudioCDManager()->update();
}

AudioCDManager::Status Sound::getCDStatus() {
	if (!_isLoomSteam && !_isLoomPCE)
		return g_system->getAudioCDManager()->getStatus();
	else {
		AudioCDManager::Status info = _loomSteamCD;
		info.playing = _mixer->isSoundHandleActive(*_loomSteamCDAudioHandle);
		return info;
	}
}

void Sound::saveLoadWithSerializer(Common::Serializer &s) {
	s.syncAsSint16LE(_currentCDSound, VER(35));
	s.syncAsSint16LE(_currentMusic, VER(35));
}


#pragma mark -
#pragma mark --- Sound resource handling ---
#pragma mark -

/*
 * TODO: The way we handle sound/music resources really is one huge hack.
 * We probably should reconsider how we do this, and maybe come up with a
 * better/cleaner solution. Even if we keep the existing code, it really
 * could stand a thorough cleanup!
 */

int ScummEngine::readSoundResource(ResId idx) {
	uint32 pos, total_size, size, tag, basetag, max_total_size;
	int pri, best_pri;
	uint32 best_size = 0, best_offs = 0;
	byte *ptr;

	debugC(DEBUG_RESOURCE, "readSoundResource(%d)", idx);

	pos = 0;

	_fileHandle->readUint32LE();
	max_total_size = _fileHandle->readUint32BE() - 8;
	basetag = _fileHandle->readUint32BE();
	total_size = _fileHandle->readUint32BE();

	debugC(DEBUG_RESOURCE, "  basetag: %s, total_size=%d", tag2str(basetag), total_size);

	switch (basetag) {
	case MKTAG('M','I','D','I'):
	case MKTAG('i','M','U','S'):
		if (_sound->_musicType != MDT_PCSPK && _sound->_musicType != MDT_PCJR) {
			_fileHandle->seek(-8, SEEK_CUR);
			_fileHandle->read(_res->createResource(rtSound, idx, total_size + 8), total_size + 8);
			return 1;
		}
		break;
	case MKTAG('S','O','U',' '):
		best_pri = -1;
		while (pos < total_size) {
			tag = _fileHandle->readUint32BE();
			size = _fileHandle->readUint32BE() + 8;
			pos += size;

			pri = -1;

			switch (tag) {
			case MKTAG('T','O','W','S'):
				pri = 16;
				break;
			case MKTAG('S','B','L',' '):
				pri = 15;
				break;
			case MKTAG('A','D','L',' '):
				pri = 1;
				if (_sound->_musicType == MDT_ADLIB || _sound->_musicType == MDT_TOWNS)
					pri = 10;
				break;
			case MKTAG('A','M','I',' '):
				pri = 3;
				break;
			case MKTAG('R','O','L',' '):
				pri = 3;
				if (_native_mt32)
					pri = 5;
				break;
			case MKTAG('G','M','D',' '):
				pri = 4;
				break;
			case MKTAG('M','A','C',' '):	// Occurs in Mac MI2, FOA
				pri = 2;
				break;
			case MKTAG('S','P','K',' '):
				pri = -1;
				if (_sound->_musicType == MDT_PCSPK || _sound->_musicType == MDT_PCJR)
					pri = 11;
				break;
			default:
				break;
			}

			// We only allow SPK resources for PC Speaker and PCJr here
			// since other resource would sound horribly with their output
			// drivers.
			if ((_sound->_musicType == MDT_PCSPK || _sound->_musicType == MDT_PCJR) && pri != 11)
				pri = -1;

			// We only allow ADL, SBL and TOWS resources when AdLib
			// or FM-Towns is used as primary audio output. This fixes some
			// odd sounds when Indy and Sophia leave Atlantis with the
			// submarine in Indy4. (Easy to check with bootparam 4061 in
			// the CD version). It seems the game only contains a ROL resource
			// for sound id 60. Formerly we tried to play that via the AdLib
			// or FM-Towns audio driver resulting in strange noises. Now we
			// behave like the original did.
			// We make an exception for Macintosh, which uses priority 2 for
			// its sound resources, and Amiga games, which feature only ROL
			// resources, since we are a doing Midi -> AdLib conversion for
			// these.
			if ((_sound->_musicType == MDT_ADLIB || _sound->_musicType == MDT_TOWNS) && pri != 16
				&& pri != 15 && pri != 10 && pri != 2 && _game.platform != Common::kPlatformAmiga)
				pri = -1;

			debugC(DEBUG_RESOURCE, "    tag: %s, total_size=%d, pri=%d", tag2str(tag), size, pri);

			if (pri > best_pri) {
				best_pri = pri;
				best_size = size;
				best_offs = _fileHandle->pos();
			}

			_fileHandle->seek(size - 8, SEEK_CUR);
		}

		if (best_pri != -1) {
			_fileHandle->seek(best_offs - 8, SEEK_SET);
			ptr = _res->createResource(rtSound, idx, best_size);
			_fileHandle->read(ptr, best_size);
			//dumpResource("sound-", idx, ptr);
			return 1;
		}
		break;
	case MKTAG('M','a','c','0'):
		_fileHandle->seek(-12, SEEK_CUR);
		total_size = _fileHandle->readUint32BE() - 8;
		ptr = _res->createResource(rtSound, idx, total_size);
		_fileHandle->read(ptr, total_size);
		//dumpResource("sound-", idx, ptr);
		return 1;

	case MKTAG('M','a','c','1'):
	case MKTAG('R','I','F','F'):
	case MKTAG('T','A','L','K'):
	case MKTAG('D','I','G','I'):
	case MKTAG('C','r','e','a'):
	case 0x460e200d:	// WORKAROUND bug # 1311447
		_fileHandle->seek(-12, SEEK_CUR);
		total_size = _fileHandle->readUint32BE();
		ptr = _res->createResource(rtSound, idx, total_size);
		_fileHandle->read(ptr, total_size - 8);
		//dumpResource("sound-", idx, ptr);
		return 1;

	case MKTAG('H','S','H','D'):
		// HE sound type without SOUN header
		_fileHandle->seek(-16, SEEK_CUR);
		total_size = max_total_size + 8;
		ptr = _res->createResource(rtSound, idx, total_size);
		_fileHandle->read(ptr, total_size);
		//dumpResource("sound-", idx, ptr);
		return 1;

	case MKTAG('F','M','U','S'): {
		// Used in 3DO version of puttputt joins the parade and probably others
		// Specifies a separate file to be used for music from what I gather.
		int tmpsize;
		Common::File dmuFile;
		char buffer[128];
		debugC(DEBUG_SOUND, "Found base tag FMUS in sound %d, size %d", idx, total_size);
		debugC(DEBUG_SOUND, "It was at position %d", _fileHandle->pos());

		_fileHandle->seek(4, SEEK_CUR);
		// HSHD size
		tmpsize = _fileHandle->readUint32BE();
		// skip to size part of the SDAT block
		_fileHandle->seek(tmpsize - 4, SEEK_CUR);
		// SDAT size
		tmpsize = _fileHandle->readUint32BE();

		// SDAT contains name of file we want
		_fileHandle->read(buffer, MIN(128, tmpsize - 8));
		// files seem to be 11 chars (8.3)
		char *p = (char *)memchr(buffer, '.', 12);
		if (!p) p = &buffer[8];
		strcpy(p, ".dmu");
		debugC(DEBUG_SOUND, "FMUS file %s", buffer);

		if (!dmuFile.open(buffer)) {
			error("Can't open music file %s", buffer);
			_res->_types[rtSound][idx]._roomoffs = RES_INVALID_OFFSET;
			return 0;
		}
		dmuFile.seek(4, SEEK_SET);
		total_size = dmuFile.readUint32BE();
		debugC(DEBUG_SOUND, "dmu file size %d", total_size);
		dmuFile.seek(-8, SEEK_CUR);
		dmuFile.read(_res->createResource(rtSound, idx, total_size), total_size);
		dmuFile.close();
		}
		return 1;

	default:
		if (SWAP_BYTES_32(basetag) == max_total_size) {
			_fileHandle->seek(-12, SEEK_CUR);
			total_size = _fileHandle->readUint32BE();
			_fileHandle->seek(-8, SEEK_CUR);
			ptr = _res->createResource(rtSound, idx, total_size);
			_fileHandle->read(ptr, total_size);
			//dumpResource("sound-", idx, ptr);
			return 1;
		}
	}

	if (total_size)
		warning("Unrecognized base tag 0x%08x in sound %d", basetag, idx);
	_res->_types[rtSound][idx]._roomoffs = RES_INVALID_OFFSET;
	return 0;
}

// AdLib MIDI-SYSEX to set MIDI instruments for small header games.
static const byte ADLIB_INSTR_MIDI_HACK[95] = {
	0x00, 0xf0, 0x14, 0x7d, 0x00,  // sysex 00: part on/off
	0x00, 0x00, 0x03,              // part/channel  (offset  5)
	0x00, 0x00, 0x07, 0x0f, 0x00, 0x00, 0x08, 0x00,
	0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0xf7,
	0x00, 0xf0, 0x41, 0x7d, 0x10,  // sysex 16: set instrument
	0x00, 0x01,                    // part/channel  (offset 28)
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0xf7,
	0x00, 0xb0, 0x07, 0x64        // Controller 7 = 100 (offset 92)
};

static const byte map_param[7] = {
	0, 2, 3, 4, 8, 9, 0,
};

static const byte freq2note[128] = {
	/*128*/	6, 6, 6, 6,
	/*132*/ 7, 7, 7, 7, 7, 7, 7,
	/*139*/ 8, 8, 8, 8, 8, 8, 8, 8, 8,
	/*148*/ 9, 9, 9, 9, 9, 9, 9, 9, 9,
	/*157*/ 10, 10, 10, 10, 10, 10, 10, 10, 10,
	/*166*/ 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
	/*176*/ 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
	/*186*/ 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	/*197*/ 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
	/*209*/ 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
	/*222*/ 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
	/*235*/ 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
	/*249*/ 18, 18, 18, 18, 18, 18, 18
};

static const uint16 num_steps_table[] = {
	1, 2, 4, 5,
	6, 7, 8, 9,
	10, 12, 14, 16,
	18, 21, 24, 30,
	36, 50, 64, 82,
	100, 136, 160, 192,
	240, 276, 340, 460,
	600, 860, 1200, 1600
};

static int convert_extraflags(byte * ptr, byte * src_ptr) {
	int flags = src_ptr[0];

	int t1, t2, t3, t4, time;
	int v1, v2, v3;

	if (!(flags & 0x80))
		return -1;

	t1 = (src_ptr[1] & 0xf0) >> 3;
	t2 = (src_ptr[2] & 0xf0) >> 3;
	t3 = (src_ptr[3] & 0xf0) >> 3 | (flags & 0x40 ? 0x80 : 0);
	t4 = (src_ptr[3] & 0x0f) << 1;
	v1 = (src_ptr[1] & 0x0f);
	v2 = (src_ptr[2] & 0x0f);
	v3 = 31;
	if ((flags & 0x7) == 0) {
	  v1 = v1 + 31 + 8;
	  v2 = v2 + 31 + 8;
	} else {
	  v1 = v1 * 2 + 31;
	  v2 = v2 * 2 + 31;
	}

	/* flags a */
	if ((flags & 0x7) == 6)
		ptr[0] = 0;
	else {
		ptr[0] = (flags >> 4) & 0xb;
		ptr[1] = map_param[flags & 0x7];
	}

	/* extra a */
	ptr[2] = 0;
	ptr[3] = 0;
	ptr[4] = t1 >> 4;
	ptr[5] = t1 & 0xf;
	ptr[6] = v1 >> 4;
	ptr[7] = v1 & 0xf;
	ptr[8] = t2 >> 4;
	ptr[9] = t2 & 0xf;
	ptr[10] = v2 >> 4;
	ptr[11] = v2 & 0xf;
	ptr[12] = t3 >> 4;
	ptr[13] = t3 & 0xf;
	ptr[14] = t4 >> 4;
	ptr[15] = t4 & 0xf;
	ptr[16] = v3 >> 4;
	ptr[17] = v3 & 0xf;

	time = num_steps_table[t1] + num_steps_table[t2]
		+ num_steps_table[t3 & 0x7f] + num_steps_table[t4];
	if (flags & 0x20) {
		int playtime = ((src_ptr[4] >> 4) & 0xf) * 118 +
			(src_ptr[4] & 0xf) * 8;
		if (playtime > time)
			time = playtime;
	}
	/*
	time = ((src_ptr[4] >> 4) & 0xf) * 118 +
		(src_ptr[4] & 0xf) * 8;
	*/
	return time;
}

#define kMIDIHeaderSize		46
static byte *writeMIDIHeader(byte *ptr, const char *type, int ppqn, int total_size) {
	uint32 dw = TO_BE_32(total_size);

	memcpy(ptr, type, 4); ptr += 4;
	memcpy(ptr, &dw, 4); ptr += 4;
	memcpy(ptr, "MDhd", 4); ptr += 4;
	ptr[0] = 0; ptr[1] = 0; ptr[2] = 0; ptr[3] = 8;
	ptr += 4;
	memset(ptr, 0, 8); ptr += 8;
	memcpy(ptr, "MThd", 4); ptr += 4;
	ptr[0] = 0; ptr[1] = 0; ptr[2] = 0; ptr[3] = 6;
	ptr += 4;
	ptr[0] = 0; ptr[1] = 0; ptr[2] = 0; ptr[3] = 1; // MIDI format 0 with 1 track
	ptr += 4;

	*ptr++ = ppqn >> 8;
	*ptr++ = ppqn & 0xFF;

	memcpy(ptr, "MTrk", 4); ptr += 4;
	memcpy(ptr, &dw, 4); ptr += 4;

	return ptr;
}

static byte *writeVLQ(byte *ptr, int value) {
	if (value > 0x7f) {
		if (value > 0x3fff) {
			*ptr++ = (value >> 14) | 0x80;
			value &= 0x3fff;
		}
		*ptr++ = (value >> 7) | 0x80;
		value &= 0x7f;
	}
	*ptr++ = value;
	return ptr;
}

static void convertADResource(ResourceManager *res, const GameSettings& game, ResId idx, byte *src_ptr, int size) {
	// We will ignore the PPQN in the original resource, because
	// it's invalid anyway. We use a constant PPQN of 480.
	const int ppqn = 480;
	uint32 dw;
	int i, ch;
	byte *ptr;
	int total_size = kMIDIHeaderSize + 7 + 8 * sizeof(ADLIB_INSTR_MIDI_HACK) + size;
	total_size += 24;	// Up to 24 additional bytes are needed for the jump sysex

	ptr = res->createResource(rtSound, idx, total_size);

	src_ptr += 2;
	size -= 2;

	// 0x80 marks a music resource. Otherwise it's a SFX
	if (*src_ptr == 0x80) {
		byte ticks, play_once;
		byte num_instr;
		byte *channel, *instr, *track;

		ptr = writeMIDIHeader(ptr, "ADL ", ppqn, total_size);

		// The "speed" of the song
		ticks = *(src_ptr + 1);

		// Flag that tells us whether we should loop the song (0) or play it only once (1)
		play_once = *(src_ptr + 2);

		// Number of instruments used
		num_instr = *(src_ptr + 8);	// Normally 8

		// copy the pointer to instrument data
		channel = src_ptr + 9;
		instr   = src_ptr + 0x11;

		// skip over the rest of the header and copy the MIDI data into a buffer
		src_ptr  += 0x11 + 8 * 16;
		size -= 0x11 + 8 * 16;

		track = src_ptr;

		// Convert the ticks into a MIDI tempo.
		// Unfortunate LOOM and INDY3 have different interpretation
		// of the ticks value.
		if (game.id == GID_INDY3) {
			// Note: since we fix ppqn at 480, ppqn/473 is almost 1
			dw = 500000 * 256 / 473 * ppqn / ticks;
		} else if (game.id == GID_LOOM && game.version == 3) {
			dw = 500000 * ppqn / 4 / ticks;
		} else {
			dw = 500000 * 256 / ticks;
		}
		debugC(DEBUG_SOUND, "  ticks = %d, speed = %d", ticks, dw);

		// Write a tempo change Meta event
		memcpy(ptr, "\x00\xFF\x51\x03", 4); ptr += 4;
		*ptr++ = (byte)((dw >> 16) & 0xFF);
		*ptr++ = (byte)((dw >> 8) & 0xFF);
		*ptr++ = (byte)(dw & 0xFF);

		// Copy our hardcoded instrument table into it
		// Then, convert the instrument table as given in this song resource
		// And write it *over* the hardcoded table.
		// Note: we deliberately.

		/* now fill in the instruments */
		for (i = 0; i < num_instr; i++) {
			ch = channel[i] - 1;
			if (ch < 0 || ch > 15)
				continue;

			if (instr[i*16 + 13])
				debugC(DEBUG_SOUND, "Sound %d instrument %d uses percussion", idx, i);

			debugC(DEBUG_SOUND, "Sound %d: instrument %d on channel %d.", idx, i, ch);

			memcpy(ptr, ADLIB_INSTR_MIDI_HACK, sizeof(ADLIB_INSTR_MIDI_HACK));

			ptr[5]  += ch;
			ptr[28] += ch;
			ptr[92] += ch;

			/* mod_characteristics */
			ptr[30 + 0] = (instr[i * 16 + 3] >> 4) & 0xf;
			ptr[30 + 1] = instr[i * 16 + 3] & 0xf;

			/* mod_scalingOutputLevel */
			ptr[30 + 2] = (instr[i * 16 + 4] >> 4) & 0xf;
			ptr[30 + 3] = instr[i * 16 + 4] & 0xf;

			/* mod_attackDecay */
			ptr[30 + 4] = ((~instr[i * 16 + 5]) >> 4) & 0xf;
			ptr[30 + 5] = (~instr[i * 16 + 5]) & 0xf;

			/* mod_sustainRelease */
			ptr[30 + 6] = ((~instr[i * 16 + 6]) >> 4) & 0xf;
			ptr[30 + 7] = (~instr[i * 16 + 6]) & 0xf;

			/* mod_waveformSelect */
			ptr[30 + 8] = (instr[i * 16 + 7] >> 4) & 0xf;
			ptr[30 + 9] = instr[i * 16 + 7] & 0xf;

			/* car_characteristic */
			ptr[30 + 10] = (instr[i * 16 + 8] >> 4) & 0xf;
			ptr[30 + 11] = instr[i * 16 + 8] & 0xf;

			/* car_scalingOutputLevel */
			ptr[30 + 12] = (instr[i * 16 + 9] >> 4) & 0xf;
			ptr[30 + 13] = instr[i * 16 + 9] & 0xf;

			/* car_attackDecay */
			ptr[30 + 14] = ((~instr[i * 16 + 10]) >> 4) & 0xf;
			ptr[30 + 15] = (~instr[i * 16 + 10]) & 0xf;

			/* car_sustainRelease */
			ptr[30 + 16] = ((~instr[i * 16 + 11]) >> 4) & 0xf;
			ptr[30 + 17] = (~instr[i * 16 + 11]) & 0xf;

			/* car_waveFormSelect */
			ptr[30 + 18] = (instr[i * 16 + 12] >> 4) & 0xf;
			ptr[30 + 19] = instr[i * 16 + 12] & 0xf;

			/* feedback */
			ptr[30 + 20] = (instr[i * 16 + 2] >> 4) & 0xf;
			ptr[30 + 21] = instr[i * 16 + 2] & 0xf;
			ptr += sizeof(ADLIB_INSTR_MIDI_HACK);
		}

		// There is a constant delay of ppqn/3 before the music starts.
		if (ppqn / 3 >= 128)
			*ptr++ = ((ppqn / 3) >> 7) | 0x80;
		*ptr++ = ppqn / 3 & 0x7f;

		// Now copy the actual music data
		memcpy(ptr, track, size);
		ptr += size;

		if (!play_once) {
			// The song is meant to be looped. We achieve this by inserting just
			// before the song end a jump to the song start. More precisely we abuse
			// a S&M sysex, "maybe_jump" to achieve this effect. We could also
			// use a set_loop sysex, but it's a bit longer, a little more complicated,
			// and has no advantage either.

			// First, find the track end
			byte *end = ptr;
			ptr -= size;
			for (; ptr < end; ptr++) {
				if (*ptr == 0xff && *(ptr + 1) == 0x2f)
					break;
			}
			assert(ptr < end);

			// Now insert the jump. The jump offset is measured in ticks.
			// We have ppqn/3 ticks before the first note.

			const int jump_offset = ppqn / 3;
			memcpy(ptr, "\xf0\x13\x7d\x30\00", 5); ptr += 5;	// maybe_jump
			memcpy(ptr, "\x00\x00", 2); ptr += 2;			// cmd -> 0 means always jump
			memcpy(ptr, "\x00\x00\x00\x00", 4); ptr += 4;	// track -> there is only one track, 0
			memcpy(ptr, "\x00\x00\x00\x01", 4); ptr += 4;	// beat -> for now, 1 (first beat)
			// Ticks
			*ptr++ = (byte)((jump_offset >> 12) & 0x0F);
			*ptr++ = (byte)((jump_offset >> 8) & 0x0F);
			*ptr++ = (byte)((jump_offset >> 4) & 0x0F);
			*ptr++ = (byte)(jump_offset & 0x0F);
			memcpy(ptr, "\x00\xf7", 2); ptr += 2;	// sysex end marker
		}
	} else {

		/* This is a sfx resource.  First parse it quickly to find the parallel
		 * tracks.
		 */
		ptr = writeMIDIHeader(ptr, "ASFX", ppqn, total_size);

		byte current_instr[3][14];
		int  current_note[3];
		int track_time[3];
		byte *track_data[3];

		int track_ctr = 0;
		byte chunk_type = 0;
		int delay, delay2, olddelay;

		// Write a tempo change Meta event
		// 473 / 4 Hz, convert to micro seconds.
		dw = 1000000 * ppqn * 4 / 473;
		memcpy(ptr, "\x00\xFF\x51\x03", 4); ptr += 4;
		*ptr++ = (byte)((dw >> 16) & 0xFF);
		*ptr++ = (byte)((dw >> 8) & 0xFF);
		*ptr++ = (byte)(dw & 0xFF);

		for (i = 0; i < 3; i++) {
			track_time[i] = -1;
			current_note[i] = -1;
		}
		while (size > 0) {
			assert(track_ctr < 3);
			track_data[track_ctr] = src_ptr;
			track_time[track_ctr] = 0;
			track_ctr++;
			while (size > 0) {
				chunk_type = *(src_ptr);
				if (chunk_type == 1) {
					src_ptr += 15;
					size -= 15;
				} else if (chunk_type == 2) {
					src_ptr += 11;
					size -= 11;
				} else if (chunk_type == 0x80) {
					src_ptr ++;
					size --;
				} else {
					break;
				}
			}
			if (chunk_type == 0xff)
				break;
			src_ptr++;
		}

		int curtime = 0;
		for (;;) {
			int mintime = -1;
			ch = -1;
			for (i = 0; i < 3; i++) {
				if (track_time[i] >= 0 &&
					(mintime == -1 || mintime > track_time[i])) {
					mintime = track_time[i];
					ch = i;
				}
			}
			if (mintime < 0)
				break;

			src_ptr = track_data[ch];
			chunk_type = *src_ptr;

			if (current_note[ch] >= 0) {
				delay = mintime - curtime;
				curtime = mintime;
				ptr = writeVLQ(ptr, delay);
				*ptr++ = 0x80 + ch; // key off channel;
				*ptr++ = current_note[ch];
				*ptr++ = 0;
				current_note[ch] = -1;
			}

			switch (chunk_type) {
			case 1:
				/* Instrument definition */
				memcpy(current_instr[ch], src_ptr + 1, 14);
				src_ptr += 15;
				break;

			case 2:
				/* tone/parammodulation */
				memcpy(ptr, ADLIB_INSTR_MIDI_HACK,
					   sizeof(ADLIB_INSTR_MIDI_HACK));

				ptr[5]  += ch;
				ptr[28] += ch;
				ptr[92] += ch;

				/* mod_characteristic */
				ptr[30 + 0] = (current_instr[ch][3] >> 4) & 0xf;
				ptr[30 + 1] = current_instr[ch][3] & 0xf;

				/* mod_scalingOutputLevel */
				ptr[30 + 2] = (current_instr[ch][4] >> 4) & 0xf;
				ptr[30 + 3] = current_instr[ch][4] & 0xf;

				/* mod_attackDecay */
				ptr[30 + 4] = ((~current_instr[ch][5]) >> 4) & 0xf;
				ptr[30 + 5] = (~current_instr[ch][5]) & 0xf;

				/* mod_sustainRelease */
				ptr[30 + 6] = ((~current_instr[ch][6]) >> 4) & 0xf;
				ptr[30 + 7] = (~current_instr[ch][6]) & 0xf;

				/* mod_waveformSelect */
				ptr[30 + 8] = (current_instr[ch][7] >> 4) & 0xf;
				ptr[30 + 9] = current_instr[ch][7] & 0xf;

				/* car_characteristic */
				ptr[30 + 10] = (current_instr[ch][8] >> 4) & 0xf;
				ptr[30 + 11] = current_instr[ch][8] & 0xf;

				/* car_scalingOutputLevel */
				ptr[30 + 12] = ((current_instr[ch][9]) >> 4) & 0xf;
				ptr[30 + 13] = (current_instr[ch][9]) & 0xf;

				/* car_attackDecay */
				ptr[30 + 14] = ((~current_instr[ch][10]) >> 4) & 0xf;
				ptr[30 + 15] = (~current_instr[ch][10]) & 0xf;

				/* car_sustainRelease */
				ptr[30 + 16] = ((~current_instr[ch][11]) >> 4) & 0xf;
				ptr[30 + 17] = (~current_instr[ch][11]) & 0xf;

				/* car_waveFormSelect */
				ptr[30 + 18] = (current_instr[ch][12] >> 4) & 0xf;
				ptr[30 + 19] = current_instr[ch][12] & 0xf;

				/* feedback */
				ptr[30 + 20] = (current_instr[ch][2] >> 4) & 0xf;
				ptr[30 + 21] = current_instr[ch][2] & 0xf;

				delay = mintime - curtime;
				curtime = mintime;

				{
					delay = convert_extraflags(ptr + 30 + 22, src_ptr + 1);
					delay2 = convert_extraflags(ptr + 30 + 40, src_ptr + 6);
					debugC(DEBUG_SOUND, "delays: %d / %d", delay, delay2);
					if (delay2 >= 0 && delay2 < delay)
						delay = delay2;
					if (delay == -1)
						delay = 0;
				}

				/* duration */
				ptr[30 + 58] = 0; // ((delay * 17 / 63) >> 4) & 0xf;
				ptr[30 + 59] = 0; // (delay * 17 / 63) & 0xf;

				ptr += sizeof(ADLIB_INSTR_MIDI_HACK);

				olddelay = mintime - curtime;
				curtime = mintime;
				ptr = writeVLQ(ptr, olddelay);

				{
					int freq = ((current_instr[ch][1] & 3) << 8)
						| current_instr[ch][0];
					if (!freq)
						freq = 0x80;
					freq <<= (((current_instr[ch][1] >> 2) + 1) & 7);
					int note = -11;
					while (freq >= 0x100) {
						note += 12;
						freq >>= 1;
					}
					debugC(DEBUG_SOUND, "Freq: %d (%x) Note: %d", freq, freq, note);
					if (freq < 0x80)
						note = 0;
					else
						note += freq2note[freq - 0x80];

					debugC(DEBUG_SOUND, "Note: %d", note);
					if (note <= 0)
						note = 1;
					else if (note > 127)
						note = 127;

					// Insert a note on event
					*ptr++ = 0x90 + ch; // key on channel
					*ptr++ = note;
					*ptr++ = 63;
					current_note[ch] = note;
					track_time[ch] = curtime + delay;
				}
				src_ptr += 11;
				break;

			case 0x80:
				// FIXME: This is incorrect. The original uses 0x80 for
				// looping a single channel. We currently interpret it as stop
				// thus we won't get looping for sound effects. It should
				// always jump to the start of the channel.
				//
				// Since we convert the data to MIDI and we cannot only loop a
				// single channel via MIDI fixing this will require some more
				// thought.
				track_time[ch] = -1;
				src_ptr ++;
				break;

			default:
				track_time[ch] = -1;
				break;
			}
			track_data[ch] = src_ptr;
		}
	}

	// Insert end of song sysex
	memcpy(ptr, "\x00\xff\x2f\x00\x00", 5); ptr += 5;
}


int ScummEngine::readSoundResourceSmallHeader(ResId idx) {
	uint32 pos, total_size, size, tag;
	uint32 ad_size = 0, ad_offs = 0;
	uint32 ro_size = 0, ro_offs = 0;
	uint32 wa_size = 0, wa_offs = 0;

	debug(4, "readSoundResourceSmallHeader(%d)", idx);

	if ((_game.id == GID_LOOM) && (_game.version == 3) && (_game.platform == Common::kPlatformDOS) && VAR(VAR_SOUNDCARD) == 4) {
		// Roland resources in Loom are tagless
		// So we add an RO tag to allow imuse to detect format
		byte *ptr, *src_ptr;
		ro_offs = _fileHandle->pos();
		ro_size = _fileHandle->readUint16LE();

		src_ptr = (byte *) calloc(ro_size - 4, 1);
		_fileHandle->seek(ro_offs + 4, SEEK_SET);
		_fileHandle->read(src_ptr, ro_size -4);

		ptr = _res->createResource(rtSound, idx, ro_size + 2);
		memcpy(ptr, "RO", 2); ptr += 2;
		memcpy(ptr, src_ptr, ro_size - 4); ptr += ro_size - 4;
		free(src_ptr);
		return 1;
	} else if (_game.features & GF_OLD_BUNDLE) {
		wa_offs = _fileHandle->pos();
		wa_size = _fileHandle->readUint16LE();
		_fileHandle->seek(wa_size - 2, SEEK_CUR);

		if (!(_game.platform == Common::kPlatformAtariST || _game.platform == Common::kPlatformMacintosh)) {
			ad_offs = _fileHandle->pos();
			ad_size = _fileHandle->readUint16LE();
		}
		_fileHandle->seek(4, SEEK_CUR);
		total_size = wa_size + ad_size;
	} else {
		total_size = size = _fileHandle->readUint32LE();
		tag = _fileHandle->readUint16LE();
		debug(4, "  tag='%c%c', size=%d", (char) (tag & 0xff),
				(char) ((tag >> 8) & 0xff), size);

		if (tag == 0x4F52) { // RO
			ro_offs = _fileHandle->pos();
			ro_size = size;
		} else {
			pos = 6;
			while (pos < total_size) {
				size = _fileHandle->readUint32LE();
				tag = _fileHandle->readUint16LE();
				debug(4, "  tag='%c%c', size=%d", (char) (tag & 0xff),
						(char) ((tag >> 8) & 0xff), size);
				pos += size;

				// MI1 and Indy3 uses one or more nested SO resources, which contains AD and WA
				// resources.
				if ((tag == 0x4441) && !(ad_offs)) { // AD
					ad_size = size;
					ad_offs = _fileHandle->pos();
				} else if ((tag == 0x4157) && !(wa_offs)) { // WA
					wa_size = size;
					wa_offs = _fileHandle->pos();
				} else { // other AD, WA and nested SO resources
					if (tag == 0x4F53) { // SO
						pos -= size;
						size = 6;
						pos += 6;
					}
				}
				_fileHandle->seek(size - 6, SEEK_CUR);
			}
		}
	}

	if ((_sound->_musicType == MDT_PCSPK || _sound->_musicType == MDT_PCJR) && wa_offs != 0) {
		if (_game.features & GF_OLD_BUNDLE) {
			_fileHandle->seek(wa_offs, SEEK_SET);
			_fileHandle->read(_res->createResource(rtSound, idx, wa_size), wa_size);
		} else {
			_fileHandle->seek(wa_offs - 6, SEEK_SET);
			_fileHandle->read(_res->createResource(rtSound, idx, wa_size + 6), wa_size + 6);
		}
		return 1;
	} else if (_sound->_musicType == MDT_CMS) {
		if (_game.features & GF_OLD_BUNDLE) {
			bool hasAdLibMusicTrack = false;

			if (ad_offs) {
				_fileHandle->seek(ad_offs + 4 + 2, SEEK_SET);
				hasAdLibMusicTrack = (_fileHandle->readByte() == 0x80);
			}

			if (hasAdLibMusicTrack) {
				_fileHandle->seek(ad_offs, SEEK_SET);
				_fileHandle->read(_res->createResource(rtSound, idx, ad_size), ad_size);
			} else {
				_fileHandle->seek(wa_offs, SEEK_SET);
				_fileHandle->read(_res->createResource(rtSound, idx, wa_size), wa_size);
			}
		} else {
			bool hasAdLibMusicTrack = false;

			if (ad_offs) {
				_fileHandle->seek(ad_offs + 2, SEEK_SET);
				hasAdLibMusicTrack = (_fileHandle->readByte() == 0x80);
			}

			if (hasAdLibMusicTrack) {
				_fileHandle->seek(ad_offs - 4, SEEK_SET);
				_fileHandle->read(_res->createResource(rtSound, idx, ad_size + 4), ad_size + 4);
			} else {
				_fileHandle->seek(wa_offs - 6, SEEK_SET);
				_fileHandle->read(_res->createResource(rtSound, idx, wa_size + 6), wa_size + 6);
			}
		}
	} else if (ad_offs != 0) {
		// AD resources have a header, instrument definitions and one MIDI track.
		// We build an 'ADL ' resource from that:
		//   8 bytes resource header
		//  16 bytes MDhd header
		//  14 bytes MThd header
		//   8 bytes MTrk header
		//   7 bytes MIDI tempo sysex
		//     + some default instruments
		if (_game.features & GF_OLD_BUNDLE) {
			ad_size -= 4;
			_fileHandle->seek(ad_offs + 4, SEEK_SET);
		} else {
			ad_size -= 6;
			_fileHandle->seek(ad_offs, SEEK_SET);
		}
		// For games using AD except Indy3 and Loom we are using our iMuse
		// implementation. See output initialization in
		// ScummEngine::setupMusic for more information.
		if (_game.id != GID_INDY3 && _game.id != GID_LOOM) {
			byte *ptr = (byte *)calloc(ad_size, 1);
			_fileHandle->read(ptr, ad_size);
			convertADResource(_res, _game, idx, ptr, ad_size);
			free(ptr);
		} else {
			_fileHandle->read(_res->createResource(rtSound, idx, ad_size), ad_size);
		}
		return 1;
	} else if (ro_offs != 0) {
		_fileHandle->seek(ro_offs - 2, SEEK_SET);
		_fileHandle->read(_res->createResource(rtSound, idx, ro_size - 4), ro_size - 4);
		return 1;
	}
	_res->_types[rtSound][idx]._roomoffs = RES_INVALID_OFFSET;
	return 0;
}


} // End of namespace Scumm
