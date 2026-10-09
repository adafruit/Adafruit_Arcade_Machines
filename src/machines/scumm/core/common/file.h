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

// fruitjam-scumm: Common::File without archives or FSNode. Files are looked
// up by name (case-insensitive) in the game folder by src/backend.

#ifndef COMMON_FILE_H
#define COMMON_FILE_H

#include "scummsys.h"
#include "noncopyable.h"
#include "str.h"
#include "stream.h"

namespace Common {

class File : public SeekableReadStream, public NonCopyable {
protected:
	/** File handle to the actual file; 0 if no file is open. */
	SeekableReadStream *_handle;

	/** The name of this file, kept for debugging purposes. */
	String _name;

public:
	File();
	virtual ~File();

	static bool exists(const String &filename);

	virtual bool open(const String &filename);
	virtual bool open(SeekableReadStream *stream, const String &name);
	virtual void close();

	bool isOpen() const;
	const char *getName() const { return _name.c_str(); }

	bool err() const override;
	void clearErr() override;
	bool eos() const override;
	int32 pos() const override;
	int32 size() const override;
	bool seek(int32 offs, int whence = SEEK_SET) override;
	uint32 read(void *dataPtr, uint32 dataSize) override;
};

} // End of namespace Common

// Provided by src/backend: open a game file for reading (NULL if missing),
// and create a file for writing (saves).
Common::SeekableReadStream *fj_open_read(const char *name);
Common::WriteStream *fj_open_write(const char *name);
bool fj_file_exists(const char *name);

#endif
