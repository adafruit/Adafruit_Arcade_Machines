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

#include "cdda.h"
#include "../common/util.h"
#include "../common/stream.h"
#include "../audio/audiostream.h"

namespace Scumm {


#pragma mark -
#pragma mark --- CDDA stream ---
#pragma mark -

#define START_OF_CDDA_DATA 800
#define BLOCK_SIZE 1177

class CDDAStream : public Audio::SeekableAudioStream {
private:
	Common::SeekableReadStream *_stream;
	DisposeAfterUse::Flag _disposeAfterUse;
	byte _shiftLeft;
	byte _shiftRight;
	uint32 _pos;
	Audio::Timestamp _length;

public:
	CDDAStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse);
	~CDDAStream() override;

	int readBuffer(int16 *buffer, const int numSamples) override;
	bool isStereo() const override { return true; }
	int getRate() const override { return 44100; }
	bool endOfData() const override { return _stream->eos(); }
	bool seek(const Audio::Timestamp &where) override;
	Audio::Timestamp getLength() const override { return _length; }
};

CDDAStream::CDDAStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse) :
	_stream(stream), _disposeAfterUse(disposeAfterUse), _pos(START_OF_CDDA_DATA) {
	_stream->seek(START_OF_CDDA_DATA, SEEK_SET);
	// The total size of CDDA.SOU is 289,808,802 bytes or (289808802 - 800) / 1177 = 246226 blocks
	// We also deduct the shift values to return the correct length
	uint32 blocks = (_stream->size() - START_OF_CDDA_DATA) / BLOCK_SIZE;
	_length = Audio::Timestamp(0, (_stream->size() - START_OF_CDDA_DATA - blocks) / (isStereo() ? 2 : 1), getRate());
}

CDDAStream::~CDDAStream() {
	if (_disposeAfterUse == DisposeAfterUse::YES)
		delete _stream;
}

bool CDDAStream::seek(const Audio::Timestamp &where) {
	const uint32 seekSample = convertTimeToStreamPos(where, getRate(), isStereo()).totalNumberOfFrames();
	uint32 blocks = seekSample / 1176;

	// Before seeking, read the shift values from the beginning of that block
	_stream->seek(START_OF_CDDA_DATA + blocks * BLOCK_SIZE, SEEK_SET);
	byte shiftVal = _stream->readByte();
	_shiftLeft = shiftVal >> 4;
	_shiftRight = shiftVal & 0x0F;

	_pos = START_OF_CDDA_DATA + blocks + seekSample;
	return _stream->seek(_pos, SEEK_SET);
}

int CDDAStream::readBuffer(int16 *buffer, const int numSamples) {
	// fruitjam-scumm: decode from chunks rather than one virtual read call
	// per byte; same output as ScummVM's loop.
	byte chunk[512];
	int samples = 0;

	while (samples < numSamples && !_stream->eos()) {
		uint32 inBlock = (_pos - START_OF_CDDA_DATA) % BLOCK_SIZE;
		if (!inBlock) {
			byte shiftVal = _stream->readByte();
			_shiftLeft = shiftVal >> 4;
			_shiftRight = shiftVal & 0x0F;
			_pos++;
			inBlock = 1;
		}
		uint32 n = MIN<uint32>(BLOCK_SIZE - inBlock, numSamples - samples);
		n = MIN<uint32>(n, sizeof(chunk)) & ~1;
		if (!n)
			break;
		uint32 got = _stream->read(chunk, n) & ~1;
		for (uint32 i = 0; i < got; i += 2) {
			buffer[samples++] = (int8)chunk[i] << _shiftLeft;
			buffer[samples++] = (int8)chunk[i + 1] << _shiftRight;
		}
		_pos += got;
		if (got < n)
			break;
	}
	return samples;
}

#pragma mark -
#pragma mark --- CDDA factory functions ---
#pragma mark -

Audio::SeekableAudioStream *makeCDDAStream(
	Common::SeekableReadStream *stream,
	DisposeAfterUse::Flag disposeAfterUse) {
	Audio::SeekableAudioStream *s = new CDDAStream(stream, disposeAfterUse);
	if (s && s->endOfData()) {
		delete s;
		return 0;
	} else {
		return s;
	}
	return 0;
}

} // End of namespace Scumm
