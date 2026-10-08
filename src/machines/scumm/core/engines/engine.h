/* fruitjam-scumm: the parts of ScummVM's Engine base class the SCUMM engine
 * uses. No GUI, no dialogs, no autosave timer.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef ENGINES_ENGINE_H
#define ENGINES_ENGINE_H

#include "../common/scummsys.h"
#include "../common/str.h"
#include "../common/error.h"

class OSystem;

namespace Audio {
class Mixer;
}
namespace Common {
class EventManager;
class SaveFileManager;
class TimerManager;
class SeekableReadStream;
class WriteStream;
}

class Engine {
public:
	OSystem *_system;
	Audio::Mixer *_mixer;

protected:
	Common::TimerManager *_timer;
	Common::EventManager *_eventMan;
	Common::SaveFileManager *_saveFileMan;

	const Common::String _targetName;

private:
	int _pauseLevel;
	uint32 _pauseStartTime;
	int32 _engineStartTime;

public:
	enum EngineFeature {
		kSupportsSubtitleOptions,
		kSupportsReturnToLauncher,
		kSupportsLoadingDuringRuntime,
		kSupportsSavingDuringRuntime,
		kSupportsChangingOptionsDuringRuntime,
		kSupportsArbitraryResolutions,
		kSupportsRTL = kSupportsReturnToLauncher
	};

	Engine(OSystem *syst);
	virtual ~Engine() {}

	virtual Common::Error run() = 0;
	virtual void errorString(const char *buf_input, char *buf_output, int buf_output_size);
	virtual bool hasFeature(EngineFeature f) const { return false; }
	virtual void syncSoundSettings() {}
	virtual void pauseEngineIntern(bool pause) {}

	virtual Common::Error loadGameState(int slot) { return Common::kUnknownError; }
	virtual bool canLoadGameStateCurrently() { return false; }
	virtual Common::Error saveGameState(int slot, const Common::String &desc, bool isAutosave = false) { return Common::kUnknownError; }
	virtual bool canSaveGameStateCurrently() { return false; }
	virtual bool canSaveAutosaveCurrently() { return false; }

	static void quitGame();
	static bool shouldQuit();

	void pauseEngine(bool pause);
	bool isPaused() const { return _pauseLevel != 0; }

	uint32 getTotalPlayTime() const;
	void setTotalPlayTime(uint32 time = 0);

	bool shouldPerformAutoSave(int lastSaveTime) { return false; }
	void checkCD() {}
	void openMainMenuDialog() {}
};

extern Engine *g_engine;

#endif
