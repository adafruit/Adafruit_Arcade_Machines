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

#include "file.h"

#include "scumm.h"

#include "../common/memstream.h"
#include "../common/substream.h"

namespace Scumm {

#pragma mark -
#pragma mark --- ScummFile ---
#pragma mark -

ScummFile::ScummFile() : _subFileStart(0), _subFileLen(0), _myEos(false) {
}

void ScummFile::setSubfileRange(int32 start, int32 len) {
	// TODO: Add sanity checks
	const int32 fileSize = File::size();
	assert(start <= fileSize);
	assert(start + len <= fileSize);
	_subFileStart = start;
	_subFileLen = len;
	seek(0, SEEK_SET);
}

void ScummFile::resetSubfile() {
	_subFileStart = 0;
	_subFileLen = 0;
	seek(0, SEEK_SET);
}

bool ScummFile::open(const Common::String &filename) {
	if (File::open(filename)) {
		resetSubfile();
		return true;
	} else {
		return false;
	}
}

bool ScummFile::openSubFile(const Common::String &filename) {
	assert(isOpen());

	// Disable the XOR encryption and reset any current subfile range
	setEnc(0);
	resetSubfile();

	// Read in the filename table and look for the specified file

	unsigned long file_off, file_len;
	char file_name[0x20+1];
	unsigned long i;

	// Get the length of the data file to use for consistency checks
	const uint32 data_file_len = size();

	// Read offset and length to the file records */
	const uint32 file_record_off = readUint32BE();
	const uint32 file_record_len = readUint32BE();

	// Do a quick check to make sure the offset and length are good
	if (file_record_off + file_record_len > data_file_len) {
		return false;
	}

	// Do a little consistancy check on file_record_length
	if (file_record_len % 0x28) {
		return false;
	}

	// Scan through the files
	for (i = 0; i < file_record_len; i += 0x28) {
		// read a file record
		seek(file_record_off + i, SEEK_SET);
		file_off = readUint32BE();
		file_len = readUint32BE();
		read(file_name, 0x20);
		file_name[0x20] = 0;

		assert(file_name[0]);
		//debug(7, "  extracting \'%s\'", file_name);

		// Consistency check. make sure the file data is in the file
		if (file_off + file_len > data_file_len) {
			return false;
		}

		if (scumm_stricmp(file_name, filename.c_str()) == 0) {
			// We got a match!
			setSubfileRange(file_off, file_len);
			return true;
		}
	}

	return false;
}


bool ScummFile::eos() const {
	return _subFileLen ? _myEos : File::eos();
}

int32 ScummFile::pos() const {
	return File::pos() - _subFileStart;
}

int32 ScummFile::size() const {
	return _subFileLen ? _subFileLen : File::size();
}

bool ScummFile::seek(int32 offs, int whence) {
	if (_subFileLen) {
		// Constrain the seek to the subfile
		switch (whence) {
		case SEEK_END:
			offs = _subFileStart + _subFileLen + offs;
			break;
		case SEEK_SET:
		default:
			offs += _subFileStart;
			break;
		case SEEK_CUR:
			offs += File::pos();
			break;
		}
		assert((int32)_subFileStart <= offs && offs <= (int32)(_subFileStart + _subFileLen));
		whence = SEEK_SET;
	}
	bool ret = File::seek(offs, whence);
	if (ret)
		_myEos = false;
	return ret;
}

uint32 ScummFile::read(void *dataPtr, uint32 dataSize) {
	uint32 realLen;

	if (_subFileLen) {
		// Limit the amount we read by the subfile boundaries.
		const int32 curPos = pos();
		assert(_subFileLen >= curPos);
		int32 newPos = curPos + dataSize;
		if (newPos > _subFileLen) {
			dataSize = _subFileLen - curPos;
			_myEos = true;
		}
	}

	realLen = File::read(dataPtr, dataSize);


	// If an encryption byte was specified, XOR the data we just read by it.
	// This simple kind of "encryption" was used by some of the older SCUMM
	// games.
	if (_encbyte) {
		byte *p = (byte *)dataPtr;
		byte *end = p + realLen;
		while (p < end)
			*p++ ^= _encbyte;
	}

	return realLen;
}

#pragma mark -
#pragma mark --- ScummSteamFile ---
#pragma mark -

bool ScummSteamFile::open(const Common::String &filename) {
	if (filename.equalsIgnoreCase(_indexFile.indexFileName)) {
		return openWithSubRange(_indexFile.executableName, _indexFile.start, _indexFile.len);
	} else {
		// Regular non-bundled file
		return ScummFile::open(filename);
	}
}

bool ScummSteamFile::openWithSubRange(const Common::String &filename, int32 subFileStart, int32 subFileLen) {
	if (ScummFile::open(filename)) {
		_subFileStart = subFileStart;
		_subFileLen = subFileLen;
		seek(0, SEEK_SET);
		return true;
	} else {
		return false;
	}
}

#pragma mark -
#pragma mark --- ScummDiskImage ---
#pragma mark -

static const int maniacResourcesPerFile[55] = {
	 0, 11,  1,  3,  9, 12,  1, 13, 10,  6,
	 4,  1,  7,  1,  1,  2,  7,  8, 19,  9,
	 6,  9,  2,  6,  8,  4, 16,  8,  3,  3,
	12, 12,  2,  8,  1,  1,  2,  1,  9,  1,
	 3,  7,  3,  3, 13,  5,  4,  3,  1,  1,
	 3, 10,  1,  0,  0
};

static const int maniacDemoResourcesPerFile[55] = {
	 0, 12,  0,  2,  1, 12,  1, 13,  6,  0,
	 31, 0,  1,  0,  0,  0,  0,  1,  1,  1,
	 0,  1,  0,  0,  2,  0,  0,  1,  0,  0,
	 2,  7,  1, 11,  0,  0,  5,  1,  0,  0,
	 1,  0,  1,  3,  4,  3,  1,  0,  0,  1,
	 2,  2,  0,  0,  0
};

static const int zakResourcesPerFile[59] = {
	 0, 29, 12, 14, 13,  4,  4, 10,  7,  4,
	14, 19,  5,  4,  7,  6, 11,  9,  4,  4,
	 1,  3,  3,  5,  1,  9,  4, 10, 13,  6,
	 7, 10,  2,  6,  1, 11,  2,  5,  7,  1,
	 7,  1,  4,  2,  8,  6,  6,  6,  4, 13,
	 3,  1,  2,  1,  2,  1, 10,  1,  1
};


static uint16 write_byte(Common::WriteStream *out, byte val) {
	val ^= 0xFF;
	if (out != 0)
		out->writeByte(val);
	return 1;
}

static uint16 write_word(Common::WriteStream *out, uint16 val) {
	val ^= 0xFFFF;
	if (out != 0)
		out->writeUint16LE(val);
	return 2;
}


} // End of namespace Scumm
