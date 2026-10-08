#include "../backend/fj_arduino.h" // Adafruit Arcade Machines build mode
/* fruitjam-scumm - SCUMM v3/v4 on the Adafruit Fruit Jam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Game detection for one game folder. A small replacement for ScummVM's
 * detection.cpp that knows only the SCUMM v3/v4 PC games (and Loom for the
 * PC Engine CD): it looks for the
 * index file (00.LFL, 000.LFL, or the index embedded in the Steam/GOG
 * Loom.exe), takes its MD5 and looks it up in fj_md5.h. The game settings
 * rows are copied from ScummVM 2.2.0's detection_tables.h.
 */

#include "../common/file.h"
#include "../common/md5.h"
#include "../common/textconsole.h"
#include "../audio/mididrv.h"

#include "scumm.h"
#include "file.h"
#include "fj_detect.h"
#include "fj_md5.h"
#include "resource.h"

namespace Scumm {

#define UNK Common::kPlatformUnknown

// From ScummVM 2.2.0 engines/scumm/detection_tables.h (gameVariantsTable).
static const GameSettings fjVariants[] = {
	{"indy3", "EGA",      "ega", GID_INDY3, 3, 0, MDT_PCSPK | MDT_PCJR | MDT_CMS | MDT_ADLIB, 0, UNK, 0},
	{"indy3", "No AdLib", "ega", GID_INDY3, 3, 0, MDT_PCSPK | MDT_PCJR,             0, UNK, 0},
	{"indy3", "VGA",      "vga", GID_INDY3, 3, 0, MDT_PCSPK | MDT_PCJR | MDT_CMS | MDT_ADLIB, GF_OLD256 | GF_FEW_LOCALS, Common::kPlatformDOS, 0},
	{"indy3", "Steam",  "steam", GID_INDY3, 3, 0, MDT_PCSPK | MDT_PCJR | MDT_CMS | MDT_ADLIB, GF_OLD256 | GF_FEW_LOCALS, UNK, 0},

	{"loom", "EGA",      "ega", GID_LOOM, 3, 0, MDT_PCSPK | MDT_PCJR | MDT_CMS | MDT_ADLIB | MDT_MIDI | MDT_PREFER_MT32, 0, UNK, 0},
	{"loom", "No AdLib", "ega", GID_LOOM, 3, 0, MDT_PCSPK | MDT_PCJR | MDT_CMS,                        0, UNK, 0},
	{"loom", "VGA",      "vga", GID_LOOM, 4, 0, MDT_NONE,                         GF_AUDIOTRACKS,             Common::kPlatformDOS, 0},
	{"loom", "Steam",  "steam", GID_LOOM, 4, 0, MDT_NONE,                         GF_AUDIOTRACKS,  UNK, 0},
	{"loom", "PC-Engine",    0, GID_LOOM, 3, 0, MDT_NONE,                         GF_AUDIOTRACKS | GF_OLD256 | GF_16BIT_COLOR, Common::kPlatformPCEngine, 0},

	{"pass", 0, 0, GID_PASS, 4, 0, MDT_PCSPK | MDT_PCJR | MDT_CMS | MDT_ADLIB, GF_16COLOR, Common::kPlatformDOS, 0},

	{"monkey", "VGA",      "vga", GID_MONKEY_VGA, 4, 0, MDT_PCSPK | MDT_PCJR | MDT_CMS | MDT_ADLIB | MDT_MIDI | MDT_PREFER_MT32, 0, UNK, 0},
	{"monkey", "EGA",      "ega", GID_MONKEY_EGA, 4, 0, MDT_PCSPK | MDT_PCJR | MDT_CMS | MDT_ADLIB | MDT_MIDI | MDT_PREFER_MT32, GF_16COLOR,     Common::kPlatformDOS, 0},
	{"monkey", "No AdLib", "ega", GID_MONKEY_EGA, 4, 0, MDT_PCSPK | MDT_PCJR,                        GF_16COLOR,     Common::kPlatformAtariST, 0},
	{"monkey", "Demo",     "ega", GID_MONKEY_EGA, 4, 0, MDT_PCSPK | MDT_PCJR | MDT_CMS | MDT_ADLIB,            GF_16COLOR,     Common::kPlatformDOS, 0},

	{0, 0, 0, 0, 0, 0, 0, 0, UNK, 0}
};

// Where the Loom index (the 8307 byte 000.LFL of the VGA talkie) sits in
// the Steam executables, from ScummVM's detection_steam.h. Other builds
// (GOG, Amazon) are found by scanning.
struct FjExe {
	const char *name;
	Common::Platform platform;
	int32 start;
};

static const FjExe fjLoomExes[] = {
	{ "Loom.exe", Common::kPlatformWindows, 187248 },
	{ "LOOMSTEAM.EXE", Common::kPlatformWindows, 187248 },
	{ "Loom", Common::kPlatformMacintosh, 170464 },
	{ 0, UNK, 0 }
};

static const int32 kLoomIndexLen = 8307;

static SteamIndexFile fjSteamIndex;
static char fjExeName[32];

const SteamIndexFile *lookUpSteamIndexFile(Common::String pattern, Common::Platform platform) {
	return fjSteamIndex.len ? &fjSteamIndex : 0;
}

static bool isTagChar(byte c) {
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
}

// Walk the block chain of a v4 index (plain, not encrypted: 4-byte size
// that counts the 6-byte header, then a 2-character tag, "RN" first). True
// if the blocks add up to exactly len bytes.
static bool isV4Index(const byte *p, int32 avail, int32 len) {
	int32 pos = 0;
	if (avail < len || p[4] != 'R' || p[5] != 'N')
		return false;
	while (pos < len) {
		if (pos + 6 > len)
			return false;
		uint32 size = READ_LE_UINT32(p + pos);
		if (!isTagChar(p[pos + 4]) || !isTagChar(p[pos + 5]) || size < 6 || size > (uint32)(len - pos))
			return false;
		pos += size;
	}
	return pos == len;
}

// Find the embedded Loom index in an executable. Tries the known offset,
// then scans the file in 32 KB windows.
static int32 findLoomIndex(Common::File &f, int32 hint) {
	byte *buf = (byte *)malloc(32768 + kLoomIndexLen);
	if (!buf)
		return -1;
	int32 found = -1;
	if (hint >= 0 && hint + kLoomIndexLen <= f.size()) {
		f.seek(hint, SEEK_SET);
		if (f.read(buf, kLoomIndexLen) == (uint32)kLoomIndexLen && isV4Index(buf, kLoomIndexLen, kLoomIndexLen))
			found = hint;
	}
	for (int32 base = 0; found < 0 && base + kLoomIndexLen <= f.size(); base += 32768) {
		f.seek(base, SEEK_SET);
		int32 n = f.read(buf, 32768 + kLoomIndexLen);
		for (int32 i = 0; i + 6 <= n && i < 32768; i++) {
			if (buf[i + 4] == 'R' && buf[i + 5] == 'N' && isV4Index(buf + i, n - i, kLoomIndexLen)) {
				found = base + i;
				break;
			}
		}
	}
	free(buf);
	return found;
}

static bool fromMD5(const Common::String &md5, DetectorResult &dr) {
	for (const MD5Table *m = md5table; m->md5; m++) {
		if (md5 != m->md5)
			continue;
		for (const GameSettings *g = fjVariants; g->gameid; g++) {
			if (strcmp(g->gameid, m->gameid))
				continue;
			if (g->variant == 0 ? m->variant[0] != 0 : strcmp(g->variant, m->variant))
				continue;
			dr.game = *g;
			if (m->platform != Common::kPlatformUnknown)
				dr.game.platform = m->platform;
			dr.language = m->language;
			dr.extra = m->extra;
			dr.md5 = md5;
			return true;
		}
	}
	return false;
}

static Common::String fileMD5(const char *name) {
	Common::File f;
	if (!f.open(name))
		return Common::String();
	return Common::computeStreamMD5AsString(f, 1024 * 1024);
}

const char *fjDetectGame(DetectorResult &dr) {
	dr.language = Common::EN_ANY;
	dr.extra = "";
	dr.fp.pattern = 0;
	fjSteamIndex.len = 0;

	// Loom from Steam/GOG: CD audio in CDDA.SOU, index inside the executable.
	for (const FjExe *e = fjLoomExes; e->name; e++) {
		Common::File f;
		if (!f.open(e->name) || f.size() < 100000)
			continue;
		int32 at = findLoomIndex(f, e->start);
		if (at < 0)
			continue;
		if (at != e->start)
			warning("%s: Loom index found at %d (not the Steam offset)", e->name, at);
		Common::strlcpy(fjExeName, e->name, sizeof(fjExeName));
		fjSteamIndex.id = GID_LOOM;
		fjSteamIndex.platform = e->platform;
		fjSteamIndex.pattern = "%03d.LFL";
		fjSteamIndex.indexFileName = "000.LFL";
		fjSteamIndex.executableName = fjExeName;
		fjSteamIndex.start = at;
		fjSteamIndex.len = kLoomIndexLen;
		for (const GameSettings *g = fjVariants; g->gameid; g++) {
			if (g->id == GID_LOOM && g->variant && !strcmp(g->variant, "Steam"))
				dr.game = *g;
		}
		dr.game.platform = e->platform;
		dr.fp.pattern = "%03d.LFL";
		dr.fp.genMethod = kGenRoomNumSteam;
		dr.md5 = "5d88b9d6a88e6f8e90cded9d01b7f082"; // the embedded 000.LFL
		dr.extra = "Steam";
		return 0;
	}

	// Plain DOS files: v4 index 000.LFL, v3 index 00.LFL.
	static const struct { const char *index; const char *pattern; } layouts[] = {
		{ "000.LFL", "%03d.LFL" },
		{ "00.LFL", "%02d.LFL" },
	};
	for (int i = 0; i < 2; i++) {
		Common::String md5 = fileMD5(layouts[i].index);
		if (md5.empty())
			continue;
		if (!fromMD5(md5, dr)) {
			warning("%s md5 %s is not a known v3/v4 PC game", layouts[i].index, md5.c_str());
			return "unknown SCUMM version (index MD5 not recognised)";
		}
		dr.fp.pattern = layouts[i].pattern;
		dr.fp.genMethod = kGenRoomNum;
		return 0;
	}
	return "no 00.LFL, 000.LFL or Loom.exe in the game folder";
}

// From ScummVM 2.2.0 engines/scumm/detection.cpp.
Common::String ScummEngine::generateFilename(const int room) const {
	const int diskNumber = (room > 0) ? _res->_types[rtRoom][room]._roomno : 0;
	Common::String result;

	if (_game.version == 4) {
		if (room == 0 || room >= 900) {
			result = Common::String::format("%03d.lfl", room);
		} else {
			result = Common::String::format("disk%02d.lec", diskNumber);
		}
	} else {
		switch (_filenamePattern.genMethod) {
		case kGenDiskNum:
		case kGenDiskNumSteam:
			result = Common::String::format(_filenamePattern.pattern, diskNumber);
			break;

		case kGenRoomNum:
		case kGenRoomNumSteam:
			result = Common::String::format(_filenamePattern.pattern, room);
			break;

		case kGenUnchanged:
			result = _filenamePattern.pattern;
			break;

		default:
			error("generateFilename: Unsupported genMethod");
		}
	}

	return result;
}

bool ScummEngine::hasFeature(EngineFeature f) const {
	return
		(f == kSupportsLoadingDuringRuntime) ||
		(f == kSupportsSavingDuringRuntime) ||
		(f == kSupportsSubtitleOptions);
}

} // End of namespace Scumm
