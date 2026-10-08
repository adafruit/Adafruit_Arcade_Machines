/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// fruitjam-scumm: save files are plain files next to the game data, written
// uncompressed through src/backend.

#ifndef COMMON_SAVEFILE_H
#define COMMON_SAVEFILE_H

#include "noncopyable.h"
#include "scummsys.h"
#include "stream.h"
#include "str-array.h"
#include "error.h"

namespace Common {

typedef SeekableReadStream InSaveFile;

class OutSaveFile : public WriteStream {
protected:
	WriteStream *_wrapped;

public:
	OutSaveFile(WriteStream *w) : _wrapped(w) {}
	virtual ~OutSaveFile() { delete _wrapped; }

	virtual bool err() const { return _wrapped->err(); }
	virtual void clearErr() { _wrapped->clearErr(); }
	virtual void finalize() { _wrapped->finalize(); }
	virtual bool flush() { return _wrapped->flush(); }
	virtual uint32 write(const void *dataPtr, uint32 dataSize) { return _wrapped->write(dataPtr, dataSize); }
	virtual int32 pos() const { return _wrapped->pos(); }
};

class SaveFileManager : NonCopyable {
public:
	OutSaveFile *openForSaving(const String &name, bool compress = true);
	InSaveFile *openForLoading(const String &name);
	InSaveFile *openRawFile(const String &name) { return openForLoading(name); }
	bool removeSavefile(const String &name) { return false; }
	StringArray listSavefiles(const String &pattern);
	Error getError() { return kNoError; }
	String popErrorDesc() { return String(); }
};

} // End of namespace Common

#endif
