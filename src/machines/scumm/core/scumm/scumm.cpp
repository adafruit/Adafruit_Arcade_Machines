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
#include "../common/debug-channels.h"
#include "../common/events.h"
#include "../common/system.h"
#include "../common/translation.h"

#include "../engines/util.h"

#include "../graphics/cursorman.h"

#include "charset.h"
#include "costume.h"
#include "file.h"
#include "object.h"
#include "players/player_ad.h"
#include "players/player_v2.h"
#include "players/player_pce.h"
#include "resource.h"
#include "scumm_v3.h"
#include "sound.h"
#include "util.h"
#include "verbs.h"

#include "../backends/audiocd/audiocd.h"

#include "../audio/mixer.h"

using Common::File;

namespace Scumm {

// Use g_scumm from error() ONLY
ScummEngine *g_scumm = 0;

ScummEngine::ScummEngine(OSystem *syst, const DetectorResult &dr)
	: Engine(syst),
	  _game(dr.game),
	  _filenamePattern(dr.fp),
	  _language(dr.language),
	  _currentScript(0xFF), // Let debug() work on init stage
	  _messageDialog(0), _pauseDialog(0), _versionDialog(0),
	  _rnd("scumm")
	  {

	if (_game.platform == Common::kPlatformPCEngine)
		_gdi = new GdiPCEngine(this);
	else
		_gdi = new Gdi(this);
	_res = new ResourceManager(this);

	// Convert MD5 checksum back into a digest
	for (int i = 0; i < 16; ++i) {
		uint tmpVal = 0;
		for (int j = 0; j < 2; j++) {
			char c = dr.md5[2*i+j];
			tmpVal = tmpVal * 16 + ((c >= 'a') ? c - 'a' + 10 : (c >= 'A') ? c - 'A' + 10 : c - '0');
		}
		_gameMD5[i] = (byte)tmpVal;
	}

	_fileHandle = 0;

	// Init all vars
	_imuse = NULL;
	_imuseDigital = NULL;
	_musicEngine = NULL;
	_townsPlayer = NULL;
	_verbs = NULL;
	_objs = NULL;
	_sound = NULL;
	memset(&vm, 0, sizeof(vm));
	_pauseDialog = NULL;
	_versionDialog = NULL;
	_fastMode = 0;
	_actors = _sortedActors = NULL;
	_arraySlot = NULL;
	_inventory = NULL;
	_newNames = NULL;
	_scummVars = NULL;
	_roomVars = NULL;
	_varwatch = 0;
	_bitVars = NULL;
	_numVariables = 0;
	_numBitVariables = 0;
	_numRoomVariables = 0;
	_numLocalObjects = 0;
	_numGlobalObjects = 0;
	_numArray = 0;
	_numVerbs = 0;
	_numFlObject = 0;
	_numInventory = 0;
	_numRooms = 0;
	_numScripts = 0;
	_numSounds = 0;
	_numCharsets = 0;
	_numNewNames = 0;
	_numGlobalScripts = 0;
	_numCostumes = 0;
	_numImages = 0;
	_numLocalScripts = 60;
	_numSprites = 0;
	_numTalkies = 0;
	_numPalettes = 0;
	_numUnk = 0;
	_curPalIndex = 0;
	_currentRoom = 0;
	_egoPositioned = false;
	_mouseAndKeyboardStat = 0;
	_leftBtnPressed = 0;
	_rightBtnPressed = 0;
	_lastInputScriptTime = 0;
	_bootParam = 0;
	_dumpScripts = false;
	_debugMode = false;
	_objectOwnerTable = NULL;
	_objectRoomTable = NULL;
	_objectStateTable = NULL;
	_numObjectsInRoom = 0;
	_userPut = 0;
	_userState = 0;
	_resourceHeaderSize = 8;
	_saveLoadFlag = 0;
	_saveLoadSlot = 0;
	_lastSaveTime = 0;
	_saveTemporaryState = false;
	memset(_localScriptOffsets, 0, sizeof(_localScriptOffsets));
	_scriptPointer = NULL;
	_scriptOrgPointer = NULL;
	_opcode = 0;
	vm.numNestedScripts = 0;
	_lastCodePtr = NULL;
	_scummStackPos = 0;
	memset(_vmStack, 0, sizeof(_vmStack));
	_fileOffset = 0;
	memset(_resourceMapper, 0, sizeof(_resourceMapper));
	_lastLoadedRoom = 0;
	_roomResource = 0;
	OF_OWNER_ROOM = 0;
	_verbMouseOver = 0;
	_classData = NULL;
	_actorToPrintStrFor = 0;
	_sentenceNum = 0;
	memset(_sentence, 0, sizeof(_sentence));
	memset(_string, 0, sizeof(_string));
	_screenB = 0;
	_screenH = 0;
	_roomHeight = 0;
	_roomWidth = 0;
	_screenHeight = 0;
	_screenWidth = 0;
	memset(_virtscr, 0, sizeof(_virtscr));
	camera.reset();
	memset(_colorCycle, 0, sizeof(_colorCycle));
	memset(_colorUsedByCycle, 0, sizeof(_colorUsedByCycle));
	_ENCD_offs = 0;
	_EXCD_offs = 0;
	_CLUT_offs = 0;
	_EPAL_offs = 0;
	_IM00_offs = 0;
	_PALS_offs = 0;
	_fullRedraw = false;
	_bgNeedsRedraw = false;
	_screenEffectFlag = false;
	_completeScreenRedraw = false;
	_disableFadeInEffect = false;
	memset(&_cursor, 0, sizeof(_cursor));
	memset(_grabbedCursor, 0, sizeof(_grabbedCursor));
	_currentCursor = 0;
	_newEffect = 0;
	_switchRoomEffect2 = 0;
	_switchRoomEffect = 0;

	_bytesPerPixel = 1;
	_doEffect = false;
	_snapScroll = false;
	_shakeEnabled = false;
	_shakeFrame = 0;
	_screenStartStrip = 0;
	_screenEndStrip = 0;
	_screenTop = 0;
	_drawObjectQueNr = 0;
	memset(_drawObjectQue, 0, sizeof(_drawObjectQue));
	_palManipStart = 0;
	_palManipEnd = 0;
	_palManipCounter = 0;
	_palManipPalette = NULL;
	_palManipIntermediatePal = NULL;
	memset(gfxUsageBits, 0, sizeof(gfxUsageBits));
	_hePalettes = NULL;
	_hePaletteSlot = 0;
	_16BitPalette = NULL;
	_shadowPalette = NULL;
	_shadowPaletteSize = 0;
	_verbPalette = NULL;
	memset(_currentPalette, 0, sizeof(_currentPalette));
	memset(_darkenPalette, 0, sizeof(_darkenPalette));
	memset(_HEV7ActorPalette, 0, sizeof(_HEV7ActorPalette));
	_palDirtyMin = 0;
	_palDirtyMax = 0;
	_haveMsg = 0;
	_haveActorSpeechMsg = false;
	_useTalkAnims = false;
	_defaultTalkDelay = 0;
	_saveSound = 0;
	memset(_extraBoxFlags, 0, sizeof(_extraBoxFlags));
	memset(_scaleSlots, 0, sizeof(_scaleSlots));
	_charset = NULL;
	_charsetColor = 0;
	memset(_charsetColorMap, 0, sizeof(_charsetColorMap));
	memset(_charsetData, 0, sizeof(_charsetData));
	_charsetBufPos = 0;
	memset(_charsetBuffer, 0, sizeof(_charsetBuffer));
	_copyProtection = false;
	_voiceMode = 0;
	_talkDelay = 0;
	_NES_lastTalkingActor = 0;
	_NES_talkColor = 0;
	_keepText = false;
	_msgCount = 0;
	_costumeLoader = NULL;
	_costumeRenderer = NULL;
	_2byteFontPtr = 0;
	_V1TalkingActor = 0;
	_NESStartStrip = 0;

	_skipDrawObject = 0;


	//
	// Init all VARS to 0xFF
	//
	VAR_KEYPRESS = 0xFF;
	VAR_SYNC = 0xFF;
	VAR_EGO = 0xFF;
	VAR_CAMERA_POS_X = 0xFF;
	VAR_HAVE_MSG = 0xFF;
	VAR_ROOM = 0xFF;
	VAR_OVERRIDE = 0xFF;
	VAR_MACHINE_SPEED = 0xFF;
	VAR_ME = 0xFF;
	VAR_NUM_ACTOR = 0xFF;
	VAR_CURRENT_LIGHTS = 0xFF;
	VAR_CURRENTDRIVE = 0xFF;	// How about merging this with VAR_CURRENTDISK?
	VAR_CURRENTDISK = 0xFF;
	VAR_TMR_1 = 0xFF;
	VAR_TMR_2 = 0xFF;
	VAR_TMR_3 = 0xFF;
	VAR_MUSIC_TIMER = 0xFF;
	VAR_ACTOR_RANGE_MIN = 0xFF;
	VAR_ACTOR_RANGE_MAX = 0xFF;
	VAR_CAMERA_MIN_X = 0xFF;
	VAR_CAMERA_MAX_X = 0xFF;
	VAR_TIMER_NEXT = 0xFF;
	VAR_VIRT_MOUSE_X = 0xFF;
	VAR_VIRT_MOUSE_Y = 0xFF;
	VAR_ROOM_RESOURCE = 0xFF;
	VAR_LAST_SOUND = 0xFF;
	VAR_CUTSCENEEXIT_KEY = 0xFF;
	VAR_OPTIONS_KEY = 0xFF;
	VAR_TALK_ACTOR = 0xFF;
	VAR_CAMERA_FAST_X = 0xFF;
	VAR_SCROLL_SCRIPT = 0xFF;
	VAR_ENTRY_SCRIPT = 0xFF;
	VAR_ENTRY_SCRIPT2 = 0xFF;
	VAR_EXIT_SCRIPT = 0xFF;
	VAR_EXIT_SCRIPT2 = 0xFF;
	VAR_VERB_SCRIPT = 0xFF;
	VAR_SENTENCE_SCRIPT = 0xFF;
	VAR_INVENTORY_SCRIPT = 0xFF;
	VAR_CUTSCENE_START_SCRIPT = 0xFF;
	VAR_CUTSCENE_END_SCRIPT = 0xFF;
	VAR_CHARINC = 0xFF;
	VAR_CHARCOUNT = 0xFF;
	VAR_WALKTO_OBJ = 0xFF;
	VAR_DEBUGMODE = 0xFF;
	VAR_HEAPSPACE = 0xFF;
	VAR_RESTART_KEY = 0xFF;
	VAR_PAUSE_KEY = 0xFF;
	VAR_MOUSE_X = 0xFF;
	VAR_MOUSE_Y = 0xFF;
	VAR_TIMER = 0xFF;
	VAR_TIMER_TOTAL = 0xFF;
	VAR_SOUNDCARD = 0xFF;
	VAR_VIDEOMODE = 0xFF;
	VAR_MAINMENU_KEY = 0xFF;
	VAR_FIXEDDISK = 0xFF;
	VAR_CURSORSTATE = 0xFF;
	VAR_USERPUT = 0xFF;
	VAR_SOUNDRESULT = 0xFF;
	VAR_TALKSTOP_KEY = 0xFF;
	VAR_FADE_DELAY = 0xFF;
	VAR_NOSUBTITLES = 0xFF;

	VAR_SOUNDPARAM = 0xFF;
	VAR_SOUNDPARAM2 = 0xFF;
	VAR_SOUNDPARAM3 = 0xFF;
	VAR_INPUTMODE = 0xFF;
	VAR_MEMORY_PERFORMANCE = 0xFF;

	VAR_VIDEO_PERFORMANCE = 0xFF;
	VAR_ROOM_FLAG = 0xFF;
	VAR_GAME_LOADED = 0xFF;
	VAR_NEW_ROOM = 0xFF;
	VAR_VERSION_KEY = 0xFF;

	VAR_V5_TALK_STRING_Y = 0xFF;

	VAR_ROOM_WIDTH = 0xFF;
	VAR_ROOM_HEIGHT = 0xFF;
	VAR_SUBTITLES = 0xFF;
	VAR_V6_EMSSPACE = 0xFF;

	VAR_CAMERA_POS_Y = 0xFF;
	VAR_CAMERA_MIN_Y = 0xFF;
	VAR_CAMERA_MAX_Y = 0xFF;
	VAR_CAMERA_THRESHOLD_X = 0xFF;
	VAR_CAMERA_THRESHOLD_Y = 0xFF;
	VAR_CAMERA_SPEED_X = 0xFF;
	VAR_CAMERA_SPEED_Y = 0xFF;
	VAR_CAMERA_ACCEL_X = 0xFF;
	VAR_CAMERA_ACCEL_Y = 0xFF;
	VAR_CAMERA_DEST_X = 0xFF;
	VAR_CAMERA_DEST_Y = 0xFF;
	VAR_CAMERA_FOLLOWED_ACTOR = 0xFF;

	VAR_LEFTBTN_DOWN = 0xFF;
	VAR_RIGHTBTN_DOWN = 0xFF;
	VAR_LEFTBTN_HOLD = 0xFF;
	VAR_RIGHTBTN_HOLD = 0xFF;

	VAR_SAVELOAD_SCRIPT = 0xFF;
	VAR_SAVELOAD_SCRIPT2 = 0xFF;

	VAR_DEFAULT_TALK_DELAY = 0xFF;
	VAR_CHARSET_MASK = 0xFF;

	VAR_CUSTOMSCALETABLE = 0xFF;
	VAR_V6_SOUNDMODE = 0xFF;

	VAR_ACTIVE_VERB = 0xFF;
	VAR_ACTIVE_OBJECT1 = 0xFF;
	VAR_ACTIVE_OBJECT2 = 0xFF;
	VAR_VERB_ALLOWED = 0xFF;

	VAR_BLAST_ABOVE_TEXT = 0xFF;
	VAR_VOICE_MODE = 0xFF;
	VAR_MUSIC_BUNDLE_LOADED = 0xFF;
	VAR_VOICE_BUNDLE_LOADED = 0xFF;

	VAR_REDRAW_ALL_ACTORS = 0xFF;
	VAR_SKIP_RESET_TALK_ACTOR = 0xFF;

	VAR_SOUND_CHANNEL = 0xFF;
	VAR_TALK_CHANNEL = 0xFF;
	VAR_SOUNDCODE_TMR = 0xFF;
	VAR_RESERVED_SOUND_CHANNELS = 0xFF;

	VAR_MAIN_SCRIPT = 0xFF;

	VAR_NUM_SCRIPT_CYCLES = 0xFF;
	VAR_SCRIPT_CYCLE = 0xFF;

	VAR_QUIT_SCRIPT = 0xFF;

	VAR_NUM_GLOBAL_OBJS = 0xFF;

	// Use g_scumm from error() ONLY
	g_scumm = this;

	// Read settings from the detector & config manager
	_debugMode = (gDebugLevel >= 0);
	_dumpScripts = ConfMan.getBool("dump_scripts");
	_bootParam = ConfMan.getInt("boot_param");
	// Boot params often need debugging switched on to work
	if (_bootParam)
		_debugMode = true;

	_copyProtection = ConfMan.getBool("copy_protection");
	if (ConfMan.getBool("demo_mode"))
		_game.features |= GF_DEMO;
	if (ConfMan.hasKey("nosubtitles")) {
		// We replaced nosubtitles *ages* ago. Just convert it silently
		debug("Configuration key 'nosubtitles' is deprecated. Converting to 'subtitles'");
		if (!ConfMan.hasKey("subtitles"))
			ConfMan.setBool("subtitles", !ConfMan.getBool("nosubtitles"));
	}

	// Make sure that at least subtitles are enabled
	if (ConfMan.getBool("speech_mute") && !ConfMan.getBool("subtitles"))
		ConfMan.setBool("subtitles", true);

	// TODO Detect subtitle only versions of scumm6 games
	if (ConfMan.getBool("speech_mute"))
		_voiceMode = 2;
	else
		_voiceMode = ConfMan.getBool("subtitles");

	_renderMode = Common::kRenderDefault;

	// Check some render mode restrictions
	if (_game.version <= 1)
		_renderMode = Common::kRenderDefault;

	switch (_renderMode) {
	case Common::kRenderHercA:
	case Common::kRenderHercG:
		if (_game.version > 2 && _game.id != GID_MONKEY_EGA)
			_renderMode = Common::kRenderDefault;
		break;

	case Common::kRenderCGA:
	case Common::kRenderEGA:
	case Common::kRenderAmiga:
		if ((_game.version >= 4 && !(_game.features & GF_16COLOR)
			&& !(_game.platform == Common::kPlatformAmiga && _renderMode == Common::kRenderEGA))
			|| (_game.features & GF_OLD256))
			_renderMode = Common::kRenderDefault;
		break;

	default:
		break;
	}

	_hexdumpScripts = false;
	_showStack = false;

	if (_game.platform == Common::kPlatformFMTowns && _game.version == 3) {	// FM-TOWNS V3 games use 320x240
		_screenWidth = 320;
		_screenHeight = 240;
	} else if (_game.version == 8 || _game.heversion >= 71) {
		// COMI uses 640x480. Likewise starting from version 7.1, HE games use
		// 640x480, too.
		_screenWidth = 640;
		_screenHeight = 480;
	} else if (_game.platform == Common::kPlatformNES) {
		_screenWidth = 256;
		_screenHeight = 240;
	} else {
		_screenWidth = 320;
		_screenHeight = 200;
	}

	_bytesPerPixel = (_game.features & GF_16BIT_COLOR) ? 2 : 1;
	uint8 sizeMult = _bytesPerPixel;


	// Allocate gfx compositing buffer (not needed for V7/V8 games).
	if (_game.version < 7)
		_compositeBuf = (byte *)malloc(_screenWidth * _screenHeight * sizeMult);
	else
		_compositeBuf = 0;

	_herculesBuf = 0;
	if (_renderMode == Common::kRenderHercA || _renderMode == Common::kRenderHercG) {
		_herculesBuf = (byte *)malloc(kHercWidth * kHercHeight);
	}

}


ScummEngine::~ScummEngine() {
	delete _musicEngine;

	_mixer->stopAll();

	if (_actors) {
		for (int i = 0; i < _numActors; ++i)
			delete _actors[i];
		delete[] _actors;
	}

	delete[] _sortedActors;

	delete[] _2byteFontPtr;
	delete _charset;
	delete _fileHandle;

	delete _sound;

	delete _costumeLoader;
	delete _costumeRenderer;

	_textSurface.free();

	free(_shadowPalette);
	free(_verbPalette);

	free(_palManipPalette);
	free(_palManipIntermediatePal);

	free(_objectStateTable);
	free(_objectRoomTable);
	free(_objectOwnerTable);
	free(_inventory);
	free(_verbs);
	free(_objs);
	free(_roomVars);
	free(_scummVars);
	free(_bitVars);
	free(_newNames);
	free(_classData);
	free(_arraySlot);

	free(_compositeBuf);
	free(_herculesBuf);

	free(_16BitPalette);


	delete _res;
	delete _gdi;
}


ScummEngine_v5::ScummEngine_v5(OSystem *syst, const DetectorResult &dr)
 : ScummEngine(syst, dr) {

	// All "classic" games (V5 and older) encrypted their data files
	// with exception of the GF_OLD256 games and the PC-Engine version
	// of Loom.
	if (!(_game.features & GF_OLD256))
		_game.features |= GF_USE_KEY;

	resetCursors();

	// Setup flashlight
	memset(&_flashlight, 0, sizeof(_flashlight));
	_flashlight.xStrips = 7;
	_flashlight.yStrips = 7;
	_flashlight.buffer = NULL;

	memset(_saveLoadVarsFilename, 0, sizeof(_saveLoadVarsFilename));

	_resultVarNumber = 0;
}

ScummEngine_v4::ScummEngine_v4(OSystem *syst, const DetectorResult &dr)
	: ScummEngine_v5(syst, dr) {
	_resourceHeaderSize = 6;
	_game.features |= GF_SMALL_HEADER;
}

ScummEngine_v3::ScummEngine_v3(OSystem *syst, const DetectorResult &dr)
	: ScummEngine_v4(syst, dr) {
	// All v3 and older games only used 16 colors with exception of the GF_OLD256 games.
	if (!(_game.features & GF_OLD256))
		_game.features |= GF_16COLOR;

	_savePreparedSavegame = NULL;
}

ScummEngine_v3::~ScummEngine_v3() {
	delete _savePreparedSavegame;
}

ScummEngine_v3old::ScummEngine_v3old(OSystem *syst, const DetectorResult &dr)
	: ScummEngine_v3(syst, dr) {
	_resourceHeaderSize = 4;
	_game.features |= GF_OLD_BUNDLE;
}



#pragma mark -
#pragma mark --- Initialization ---
#pragma mark -

Common::Error ScummEngine::init() {
	// fruitjam-scumm: the backend looks files up in the game folder itself,
	// so there are no search paths, container files or CD checks here.
	if (_filenamePattern.genMethod == kGenDiskNumSteam || _filenamePattern.genMethod == kGenRoomNumSteam) {
		// Steam game versions have the index file embedded in the main executable
		const SteamIndexFile *indexFile = lookUpSteamIndexFile(_filenamePattern.pattern, _game.platform);
		if (!indexFile || indexFile->id != _game.id) {
			error("Couldn't find index file description for Steam version");
		} else {
			_fileHandle = new ScummSteamFile(*indexFile);
		}
	} else {
		// Regular access, no container file involved
		_fileHandle = new ScummFile();
	}

	// Steam Win and Mac versions share the same DOS data files. We show Windows or Mac
	// for the platform the detector, but internally we force the platform to DOS, so that
	// the code for handling the original DOS data files is used.
	if (_filenamePattern.genMethod == kGenDiskNumSteam || _filenamePattern.genMethod == kGenRoomNumSteam)
		_game.platform = Common::kPlatformDOS;

	_useCJKMode = false;
	_textSurfaceMultiplier = 1;

	// Initialize backend. fruitjam-scumm: Loom PC-Engine draws 16-bit like
	// upstream's USE_RGB_COLOR build, in RGB565 (the board's framebuffer).
	if (_game.features & GF_16BIT_COLOR) {
		Graphics::PixelFormat fmt(2, 5, 6, 5, 0, 11, 5, 0, 0);
		initGraphics(_screenWidth, _screenHeight, &fmt);
	} else {
		initGraphics(_screenWidth, _screenHeight);
	}

	_outputPixelFormat = _system->getScreenFormat();

	setupScumm();

	readIndexFile();

	resetScumm();
	resetScummVars();

	if (_game.version >= 5 && _game.version <= 7)
		_sound->setupSound();

	syncSoundSettings();

	return Common::kNoError;
}

void ScummEngine::setupScumm() {
	// Create the sound manager
	_sound = new Sound(this, _mixer);

	// Setup the music engine
	setupMusic(_game.midi);

	// Create the charset renderer
	setupCharsetRenderer();

	// Create and clear the text surface
	_textSurface.create(_screenWidth * _textSurfaceMultiplier, _screenHeight * _textSurfaceMultiplier, Graphics::PixelFormat::createFormatCLUT8());
	clearTextSurface();

	// Create the costume renderer
	setupCostumeRenderer();

	// Load game from specified slot, if any
	if (ConfMan.hasKey("save_slot")) {
		requestLoad(ConfMan.getInt("save_slot"));
	}

	_res->allocResTypeData(rtBuffer, 0, 10, kDynamicResTypeMode);

	setupScummVars();

	setupOpcodes();

	_numActors = 13;
	OF_OWNER_ROOM = 0x0F;

	// if (_game.id==GID_MONKEY2 && _bootParam == 0)
	//	_bootParam = 10001;

	// fruitjam-scumm: the 2 MB arena has room for a bigger resource cache
	// than ScummVM's 400/550 KB, which saves SD card reads on room changes.
	_res->setHeapThreshold(800000, 1100000);

	free(_compositeBuf);
	_compositeBuf = (byte *)malloc(_screenWidth * _textSurfaceMultiplier * _screenHeight * _textSurfaceMultiplier * _outputPixelFormat.bytesPerPixel);
}


void ScummEngine::setupCharsetRenderer() {
	if (_game.platform == Common::kPlatformPCEngine)
		_charset = new CharsetRendererPCE(this);
	else if (_game.version == 3)
		_charset = new CharsetRendererV3(this);
	else
		_charset = new CharsetRendererClassic(this);
}

void ScummEngine::setupCostumeRenderer() {
	if (_game.platform == Common::kPlatformPCEngine)
		_costumeRenderer = new PCEngineCostumeRenderer(this);
	else
		_costumeRenderer = new ClassicCostumeRenderer(this);
	_costumeLoader = new ClassicCostumeLoader(this);
}

void ScummEngine::resetScumm() {
	int i;

	debug(9, "resetScumm");

	if (_game.features & GF_16BIT_COLOR)
		_16BitPalette = (uint16 *)calloc(512, sizeof(uint16));

	// Indy4 Amiga needs another palette map for the verb area.
	if (_game.platform == Common::kPlatformAmiga && _game.id == GID_INDY4 && !_verbPalette)
		_verbPalette = (uint8 *)calloc(256, 1);


	initScreens(16, 144);

	_palManipCounter = 0;

	for (i = 0; i < 256; i++)
		_roomPalette[i] = i;

	resetPalette();
	if (_game.version == 1) {
	} else if (_game.features & GF_16COLOR) {
		for (i = 0; i < 16; i++)
			_shadowPalette[i] = i;
	}

	if (_game.version >= 4 && _game.version <= 7)
		loadCharset(1);

	if (_game.features & GF_OLD_BUNDLE)
		loadCharset(0);

	setShake(0);
	_cursor.animate = 1;

	// Allocate and Initialize actors
	Actor::kInvalidBox = ((_game.features & GF_SMALL_HEADER) ? kOldInvalidBox : kNewInavlidBox);
	_actors = new Actor * [_numActors];
	_sortedActors = new Actor * [_numActors];
	for (i = 0; i < _numActors; ++i) {
		if (_game.version == 3)
			_actors[i] = new Actor_v3(this, i);
		else
			_actors[i] = new Actor(this, i);
		_actors[i]->initActor(-1);
	}

	vm.numNestedScripts = 0;
	vm.cutSceneStackPointer = 0;

	memset(vm.cutScenePtr, 0, sizeof(vm.cutScenePtr));
	memset(vm.cutSceneData, 0, sizeof(vm.cutSceneData));

	for (i = 0; i < _numVerbs; i++) {
		_verbs[i].verbid = 0;
		_verbs[i].curRect.right = _screenWidth - 1;
		_verbs[i].oldRect.left = -1;
		_verbs[i].type = 0;
		_verbs[i].color = 2;
		_verbs[i].hicolor = 0;
		_verbs[i].charset_nr = 1;
		_verbs[i].curmode = 0;
		_verbs[i].saveid = 0;
		_verbs[i].center = 0;
		_verbs[i].key = 0;
	}

	camera._leftTrigger = 10;
	camera._rightTrigger = 30;
	camera._mode = 0;
	camera._follows = 0;

	_virtscr[0].xstart = 0;

	_mouse.x = 104;
	_mouse.y = 56;

	_ENCD_offs = 0;
	_EXCD_offs = 0;

	_currentScript = 0xFF;
	_sentenceNum = 0;

	_currentRoom = 0;
	_numObjectsInRoom = 0;
	_actorToPrintStrFor = 0;

	_charsetBufPos = 0;
	_haveMsg = 0;
	_haveActorSpeechMsg = false;

	_varwatch = -1;
	_screenStartStrip = 0;

	_defaultTalkDelay = 3;
	_talkDelay = 0;
	_keepText = false;
	_nextLeft = 0;
	_nextTop = 0;

	_currentCursor = 0;
	_cursor.state = 0;
	_userPut = 0;

	_newEffect = 129;
	_fullRedraw = true;

	clearDrawObjectQueue();

	for (i = 0; i < 6; i++) {
		if (_game.version == 3) { // FIXME - what is this?
			_string[i]._default.xpos = 0;
			_string[i]._default.ypos = 0;
		} else {
			_string[i]._default.xpos = 2;
			_string[i]._default.ypos = 5;
		}
		_string[i]._default.right = _screenWidth - 1;
		_string[i]._default.height = 0;
		_string[i]._default.color = 0xF;
		_string[i]._default.center = 0;
		_string[i]._default.charset = 0;
	}

	// all keys are released
	for (i = 0; i < 512; i++)
		_keyDownMap[i] = false;

	_lastSaveTime = _system->getMillis();
}

void ScummEngine_v3::resetScumm() {
	ScummEngine_v4::resetScumm();

	if (_game.id == GID_LOOM && _game.platform == Common::kPlatformPCEngine) {
		// Load tile set and palette for the distaff
		byte *roomptr = getResourceAddress(rtRoom, 90);
		assert(roomptr);
		const byte *palPtr = findResourceData(MKTAG('C','L','U','T'), roomptr);
		assert(palPtr - 4);
		setPCEPaletteFromPtr(palPtr);
		_gdi->_distaff = true;
		_gdi->loadTiles(roomptr);
		_gdi->_distaff = false;
	}

	delete _savePreparedSavegame;
	_savePreparedSavegame = NULL;
}

void ScummEngine_v4::resetScumm() {
	ScummEngine::resetScumm();

	// WORKAROUND for bug in boot script of Loom (CD)
	// The boot script sets the characters of string 21,
	// before creating the string.resource.
	if (_game.id == GID_LOOM) {
		_res->createResource(rtString, 21, 12);
	}
}


void ScummEngine::setupMusic(int midi) {
	// fruitjam-scumm: no MIDI and no iMuse. The "music" setting picks
	// "adlib" (Player_AD, Loom and Indy3 DOS only), "pcspk" (Player_V2)
	// or "none". CD audio games (Loom talkie) need no music engine.
	Common::String want = ConfMan.get("music");
	_native_mt32 = false;
	_enable_gs = false;
	_sound->_musicType = MDT_NONE;

	// Loom PC-Engine: CD tracks for music, the HuC6280 PSG for effects.
	if (_game.platform == Common::kPlatformPCEngine) {
		if (want != "none")
			_musicEngine = new Player_PCE(this, _mixer);
		return;
	}

	if (_game.features & GF_AUDIOTRACKS || want == "none" || !(midi & (MDT_ADLIB | MDT_PCSPK)))
		return;

#ifdef FJ_ADLIB
	if (want == "adlib" && (midi & MDT_ADLIB) && _game.platform == Common::kPlatformDOS &&
	    (_game.id == GID_LOOM || _game.id == GID_INDY3)) {
		_sound->_musicType = MDT_ADLIB;
		_musicEngine = new Player_AD(this);
	} else
#endif
	if (midi & MDT_PCSPK) {
		_sound->_musicType = MDT_PCSPK;
		_musicEngine = new Player_V2(this, _mixer, false);
	}
}

void ScummEngine::syncSoundSettings() {
	// Sync the engine with the config manager
	int soundVolumeMusic = ConfMan.getInt("music_volume");
	int soundVolumeSfx = ConfMan.getInt("sfx_volume");

	bool mute = false;

	if (ConfMan.hasKey("mute")) {
		mute = ConfMan.getBool("mute");

		if (mute)
			soundVolumeMusic = soundVolumeSfx = 0;
	}

	if (_musicEngine) {
		_musicEngine->setMusicVolume(soundVolumeMusic);
	}

	if (ConfMan.getBool("speech_mute"))
		_voiceMode = 2;
	else
		_voiceMode = ConfMan.getBool("subtitles");

	if (VAR_VOICE_MODE != 0xFF)
		VAR(VAR_VOICE_MODE) = _voiceMode;

	if (ConfMan.hasKey("talkspeed", _targetName)) {
		_defaultTalkDelay = getTalkSpeed();
		if (VAR_CHARINC != 0xFF)
			VAR(VAR_CHARINC) = _defaultTalkDelay;
	}

}

void ScummEngine::setTalkSpeed(int talkspeed) {
	ConfMan.setInt("talkspeed", (talkspeed * 255 + 9 / 2) / 9);
}

int ScummEngine::getTalkSpeed() {
	return (ConfMan.getInt("talkspeed") * 9 + 255 / 2) / 255;
}


#pragma mark -
#pragma mark --- Main loop ---
#pragma mark -

Common::Error ScummEngine::go() {
	setTotalPlayTime();

	// If requested, load a save game instead of running the boot script
	if (_saveLoadFlag != 2 || !loadState(_saveLoadSlot, _saveTemporaryState)) {
		_saveLoadFlag = 0;
		runBootscript();
	} else {
		_saveLoadFlag = 0;
	}

	int diff = 0;	// Duration of one loop iteration

	while (!shouldQuit()) {
		// Randomize the PRNG by calling it at regular intervals. This ensures
		// that it will be in a different state each time you run the program.
		_rnd.getRandomNumber(2);

		// Notify the script about how much time has passed, in ticks (60 ticks per second)
		if (VAR_TIMER != 0xFF)
			VAR(VAR_TIMER) = diff * 60 / 1000;
		if (VAR_TIMER_TOTAL != 0xFF)
			VAR(VAR_TIMER_TOTAL) += diff * 60 / 1000;

		// Determine how long to wait before the next loop iteration should start
		int delta = (VAR_TIMER_NEXT != 0xFF) ? VAR(VAR_TIMER_NEXT) : 4;
		if (delta < 1)	// Ensure we don't get into an endless loop
			delta = 1;  // by not decreasing sleepers.

		// Wait...
		waitForTimer(delta * 1000 / 60 - diff);

		// Start the stop watch!
		diff = _system->getMillis();

		// Run the main loop
		scummLoop(delta);

		// Halt the stop watch and compute how much time this iteration took.
		diff = _system->getMillis() - diff;


		if (shouldQuit()) {
			// TODO: Maybe perform an autosave on exit?
			runQuitScript();
		}
	}

	return Common::kNoError;
}

void ScummEngine::waitForTimer(int msec_delay) {
	uint32 start_time;

	if (_fastMode & 2)
		msec_delay = 0;
	else if (_fastMode & 1)
		msec_delay = 10;

	start_time = _system->getMillis();

	while (!shouldQuit()) {
		_sound->updateCD(); // Loop CD Audio if needed
		parseEvents();


		_system->updateScreen();
		if (_system->getMillis() >= start_time + msec_delay)
			break;
		_system->delayMillis(10);
	}
}

void ScummEngine::scummLoop(int delta) {
	if (_game.version >= 3) {
		VAR(VAR_TMR_1) += delta;
		VAR(VAR_TMR_2) += delta;
		VAR(VAR_TMR_3) += delta;
		if ((_game.id == GID_INDY3 && _game.platform != Common::kPlatformMacintosh) ||
			_game.id == GID_ZAK) {
			// Amiga/PC versions of Indy3 set three extra timers
			// FM-TOWNS version of Zak sets three extra timers
			VAR(39) += delta;
			VAR(40) += delta;
			VAR(41) += delta;
		}
	}

	if (delta > 15)
		delta = 15;

	decreaseScriptDelay(delta);

	_talkDelay -= delta;
	if (_talkDelay < 0)
		_talkDelay = 0;

	// Record the current ego actor before any scripts (including input scripts)
	// get a chance to run.
	int oldEgo = 0;
	if (VAR_EGO != 0xFF)
		oldEgo = VAR(VAR_EGO);

	// In V1-V3 games, CHARSET_1 is called much earlier than in newer games.
	// See also bug #770042 for a case were this makes a difference.
	if (_game.version <= 3)
		CHARSET_1();

	processInput();

	scummLoop_updateScummVars();

	if (_game.features & GF_AUDIOTRACKS) {
		// Covered automatically by the Sound class
	} else if (VAR_MUSIC_TIMER != 0xFF) {
		if (_musicEngine) {
			// The music engine generates the timer data for us.
			VAR(VAR_MUSIC_TIMER) = _musicEngine->getMusicTimer();
		}
	}

	// Trigger autosave if necessary.
	if (!_saveLoadFlag && shouldPerformAutoSave(_lastSaveTime) && canSaveGameStateCurrently()) {
		_saveLoadSlot = 0;
		_saveLoadDescription = Common::String::format("Autosave %d", _saveLoadSlot);
		_saveLoadFlag = 1;
		_saveTemporaryState = false;
	}

	if (VAR_GAME_LOADED != 0xFF)
		VAR(VAR_GAME_LOADED) = 0;
load_game:
	scummLoop_handleSaveLoad();

	if (_completeScreenRedraw) {
		clearCharsetMask();
		_charset->_hasMask = false;

		// HACK as in game save stuff isn't supported currently
		if (_game.id == GID_LOOM) {
			int args[NUM_SCRIPT_LOCAL];
			uint var;
			memset(args, 0, sizeof(args));
			args[0] = 2;

			if (_game.platform == Common::kPlatformMacintosh)
				var = 105;
			// 256 color CD version and PC engine version
			else if (_game.platform == Common::kPlatformPCEngine || _game.version == 4)
				var = 150;
			else
				var = 100;
			byte restoreScript = (_game.platform == Common::kPlatformFMTowns) ? 17 : 18;
			// if verbs should be shown restore them
			if (VAR(var) == 2)
				runScript(restoreScript, 0, 0, args);
		} else if (_game.version > 3) {
			for (int i = 0; i < _numVerbs; i++)
				drawVerb(i, 0);
		} else {
			redrawVerbs();
		}

		handleMouseOver(false);

		_completeScreenRedraw = false;
		_fullRedraw = true;
	}

	runAllScripts();
	checkExecVerbs();
	checkAndRunSentenceScript();

	if (shouldQuit())
		return;

	// HACK: If a load was requested, immediately perform it. This avoids
	// drawing the current room right after the load is request but before
	// it is performed. That was annoying esp. if you loaded while a SMUSH
	// cutscene was playing.
	if (_saveLoadFlag && _saveLoadFlag != 1) {
		goto load_game;
	}


	if (_currentRoom == 0) {
		if (_game.version > 3)
			CHARSET_1();
		drawDirtyScreenParts();
	} else {
		walkActors();
		moveCamera();
		updateObjectStates();
		if (_game.version > 3)
			CHARSET_1();

		scummLoop_handleDrawing();

		scummLoop_handleActors();

		_fullRedraw = false;

		scummLoop_handleEffects();

		if (VAR_MAIN_SCRIPT != 0xFF && VAR(VAR_MAIN_SCRIPT) != 0) {
			runScript(VAR(VAR_MAIN_SCRIPT), 0, 0, 0);
		}

		// Handle mouse over effects (for verbs).
		handleMouseOver(oldEgo != VAR(VAR_EGO));

		// Render everything to the screen.
		updatePalette();
		drawDirtyScreenParts();

		// FIXME / TODO: Try to move the following to scummLoop_handleSound or
		// scummLoop_handleActors (but watch out for regressions!)
		if (_game.version <= 5)
			playActorSounds();
	}

	scummLoop_handleSound();

	camera._last = camera._cur;

	_res->increaseExpireCounter();

	animateCursor();

	/* show or hide mouse */
	CursorMan.showMouse(_cursor.state > 0);
}


void ScummEngine::scummLoop_updateScummVars() {
	VAR(VAR_CAMERA_POS_X) = camera._cur.x;
	VAR(VAR_HAVE_MSG) = _haveMsg;

	VAR(VAR_VIRT_MOUSE_X) = _virtualMouse.x;
	VAR(VAR_VIRT_MOUSE_Y) = _virtualMouse.y;
	VAR(VAR_MOUSE_X) = _mouse.x;
	VAR(VAR_MOUSE_Y) = _mouse.y;
	if (VAR_DEBUGMODE != 0xFF) {
		// This is NOT for the Mac version of Indy3/Loom
		VAR(VAR_DEBUGMODE) = (_debugMode ? 1 : 0);
	}
}

void ScummEngine::scummLoop_handleSaveLoad() {
	if (_saveLoadFlag) {
		bool success;
		const char *errMsg = 0;

		if (_game.version == 8 && _saveTemporaryState)
			VAR(VAR_GAME_LOADED) = 0;

		Common::String filename;
		if (_saveLoadFlag == 1) {
			success = saveState(_saveLoadSlot, _saveTemporaryState, filename);
			if (!success)
				errMsg = _("Failed to save game to file:\n\n%s");

			if (success && _saveTemporaryState && VAR_GAME_LOADED != 0xFF && _game.version <= 7)
				VAR(VAR_GAME_LOADED) = 201;

			if (!_saveTemporaryState)
				_lastSaveTime = _system->getMillis();
		} else {
			success = loadState(_saveLoadSlot, _saveTemporaryState, filename);
			if (!success)
				errMsg = _("Failed to load saved game from file:\n\n%s");

			if (success && _saveTemporaryState && VAR_GAME_LOADED != 0xFF)
				VAR(VAR_GAME_LOADED) = (_game.version == 8) ? 1 : 203;
		}

		if (!success) {
			displayMessage(0, errMsg, filename.c_str());
		} else if (_saveLoadFlag == 1 && _saveLoadSlot != 0 && !_saveTemporaryState) {
			// Display "Save successful" message, except for auto saves
			char buf[256];
			snprintf(buf, sizeof(buf), _("Successfully saved game in file:\n\n%s"), filename.c_str());
			warning("%s", buf);
		}
		if (success && _saveLoadFlag != 1)
			clearClickedStatus();

		_saveLoadFlag = 0;
	}
}

void ScummEngine_v4::scummLoop_handleSaveLoad() {
	// copy saveLoadFlag as handleSaveLoad() resets it
	byte saveLoad = _saveLoadFlag;

	ScummEngine_v5::scummLoop_handleSaveLoad();

	// update IQ points after loading
	if (saveLoad == 2) {
		if (_game.id == GID_INDY3)
			updateIQPoints();
	}
}

void ScummEngine_v5::scummLoop_handleSaveLoad() {
	// copy saveLoadFlag as handleSaveLoad() resets it
	byte saveLoad = _saveLoadFlag;

	ScummEngine::scummLoop_handleSaveLoad();

	// update IQ points after loading
	if (saveLoad == 2) {
		if (_game.id == GID_INDY4)
			runScript(145, 0, 0, 0);
	}
}


void ScummEngine::scummLoop_handleDrawing() {
	if (camera._cur != camera._last || _bgNeedsRedraw || _fullRedraw) {
		_V0Delay._screenScroll = true;

		redrawBGAreas();
	}

	processDrawQue();
}



void ScummEngine_v5::scummLoop_handleActors() {
	setActorRedrawFlags();
	resetActorBgs();

	if (!(getCurrentLights() & LIGHTMODE_room_lights_on) &&
		  getCurrentLights() & LIGHTMODE_flashlight_on) {
		drawFlashlight();
		setActorRedrawFlags();
	}

	processActors();
}

void ScummEngine::scummLoop_handleEffects() {
	if (_game.version >= 4)
		cyclePalette();
	palManipulate();
	if (_doEffect) {
		_doEffect = false;
		fadeIn(_newEffect);
		clearClickedStatus();
	}
}

void ScummEngine::scummLoop_handleSound() {
	_sound->processSound();
}



#pragma mark -
#pragma mark --- SCUMM ---
#pragma mark -

void ScummEngine::pauseGame() {
	// fruitjam-scumm: no pause dialog. Sound stops and the game waits for a
	// key or a click, which is swallowed.
	warning("Game paused. Press any key or click to continue.");
	pauseEngine(true);
	bool resume = false;
	while (!resume && !shouldQuit()) {
		_system->delayMillis(10);
		Common::Event event;
		while (_eventMan->pollEvent(event)) {
			if (event.type == Common::EVENT_KEYDOWN || event.type == Common::EVENT_LBUTTONDOWN ||
			    event.type == Common::EVENT_RBUTTONDOWN)
				resume = true;
		}
	}
	pauseEngine(false);
}

void ScummEngine::restart() {
	// FIXME: This function *leaks memory*, and quite a lot so. For example,
	// we re-init the resource manager, which causes readMAXS() to be called
	// again, which allocates some memory. There are many other leaks, though.

	// TODO: We should also probably be reinitting a lot more stuff.

	// Fingolfin seez: An alternate way to implement restarting would be to create
	// a save state right after startup ... to this end we could introduce a SaveFile
	// subclass which is implemented using a memory buffer (i.e. no actual file is
	// created). Then to restart we just have to load that pseudo save state.


	int i;

	// Reset some stuff
	_currentRoom = 0;
	_currentScript = 0xFF;
	killAllScriptsExceptCurrent();
	setShake(0);
	_sound->stopAllSounds();

	// Clear the script variables
	for (i = 0; i < _numVariables; i++)
		_scummVars[i] = 0;

	// Empty inventory
	for (i = 1; i < _numGlobalObjects; i++)
		clearOwnerOf(i);

	readIndexFile();

	// Reinit scumm variables
	resetScumm();
	resetScummVars();

	// Reinit sound engine
	if (_game.version >= 5 && _game.version <= 7)
		_sound->setupSound();

	// Re-run bootscript
	runBootscript();
}

void ScummEngine::runBootscript() {
	int args[NUM_SCRIPT_LOCAL];
	memset(args, 0, sizeof(args));
	args[0] = _bootParam;
	if (_game.id == GID_MANIAC && (_game.features & GF_DEMO) && (_game.platform != Common::kPlatformC64))
		runScript(9, 0, 0, args);
	else
		runScript(1, 0, 0, args);
}


#pragma mark -
#pragma mark --- GUI ---
#pragma mark -

void ScummEngine::pauseEngineIntern(bool pause) {
	if (pause) {
		// Pause sound & video
		_oldSoundsPaused = _sound->_soundsPaused;
		_sound->pauseSounds(true);

	} else {
		// Update the screen to make it less likely that the player will see a
		// brief cursor palette glitch when the GUI is disabled.


		_system->updateScreen();

		// Resume sound & video
		_sound->pauseSounds(_oldSoundsPaused);
	}
}


char ScummEngine::displayMessage(const char *altButton, const char *message, ...) {
	char buf[STRINGBUFLEN];
	va_list va;

	va_start(va, message);
	vsnprintf(buf, STRINGBUFLEN, message, va);
	va_end(va);

	// fruitjam-scumm: no dialogs, the message goes to the log.
	warning("%s", buf);
	return 0;
}


#pragma mark -
#pragma mark --- Miscellaneous ---
#pragma mark -

void ScummEngine::errorString(const char *buf1, char *buf2, int buf2Size) {
	if (_currentScript != 0xFF) {
		snprintf(buf2, buf2Size, "(%d:%d:0x%lX): %s", _roomResource,
			vm.slot[_currentScript].number, (long)(_scriptPointer - _scriptOrgPointer), buf1);
	} else {
		strncpy(buf2, buf1, buf2Size);
		if (buf2Size > 0)
			buf2[buf2Size-1] = '\0';
	}
}


} // End of namespace Scumm
