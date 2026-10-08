#include "../backend/fj_arduino.h" // Adafruit Arcade Machines build mode
/* fruitjam-scumm - SCUMM v3/v4 on the Adafruit Fruit Jam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CD audio tracks for Loom PC-Engine, ripped and converted ahead of time
 * to IMA ADPCM WAV files (tools/pce_tracks.sh): 4 bits a sample, decoded a
 * block at a time, so a track streams from the SD card at 11 KB/s (22050 Hz
 * mono) with no big buffers.
 */

#include "fj_cdtrack.h"
#include "../common/endian.h"
#include "../common/stream.h"
#include "../common/textconsole.h"
#include "../common/util.h"
#include "../audio/audiostream.h"

namespace Scumm {

static const int8 imaIndexTable[16] = {
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8
};

static const uint16 imaStepTable[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
	19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
	130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
	337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
	876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
	2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
	5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

struct ImaState {
	int32 sample;
	int32 index;
};

static inline int16 imaDecode(ImaState &s, byte nibble) {
	int32 step = imaStepTable[s.index];
	int32 diff = step >> 3;
	if (nibble & 1)
		diff += step >> 2;
	if (nibble & 2)
		diff += step >> 1;
	if (nibble & 4)
		diff += step;
	if (nibble & 8)
		diff = -diff;
	s.sample = CLIP<int32>(s.sample + diff, -32768, 32767);
	s.index = CLIP<int32>(s.index + imaIndexTable[nibble], 0, 88);
	return (int16)s.sample;
}

class FjCDTrackStream : public Audio::AudioStream {
public:
	FjCDTrackStream(Common::SeekableReadStream *stream, int channels, int rate, int blockAlign,
	                uint32 dataStart, uint32 dataEnd, uint32 maxFrames);
	~FjCDTrackStream() override;

	int readBuffer(int16 *buffer, const int numSamples) override;
	bool isStereo() const override { return _channels == 2; }
	int getRate() const override { return _rate; }
	bool endOfData() const override { return _done; }

private:
	bool decodeBlock();

	Common::SeekableReadStream *_stream;
	int _channels, _rate, _blockAlign;
	uint32 _pos, _dataEnd;
	uint32 _framesLeft;
	byte *_block;
	int16 *_pcm;     // one decoded block, interleaved
	int _pcmLen, _pcmPos;
	bool _done;
};

FjCDTrackStream::FjCDTrackStream(Common::SeekableReadStream *stream, int channels, int rate, int blockAlign,
                                 uint32 dataStart, uint32 dataEnd, uint32 maxFrames)
	: _stream(stream), _channels(channels), _rate(rate), _blockAlign(blockAlign),
	  _pos(dataStart), _dataEnd(dataEnd), _framesLeft(maxFrames), _pcmLen(0), _pcmPos(0), _done(false) {
	int framesPerBlock = (blockAlign - 4 * channels) * 2 / channels + 1;
	_block = (byte *)malloc(blockAlign);
	_pcm = (int16 *)malloc(framesPerBlock * channels * sizeof(int16));
	if (!_block || !_pcm)
		_done = true;
	_stream->seek(dataStart, SEEK_SET);
}

FjCDTrackStream::~FjCDTrackStream() {
	free(_block);
	free(_pcm);
	delete _stream;
}

// One MS IMA ADPCM block: a 4-byte header per channel (first sample, step
// index), then 4-byte groups that alternate between channels, low nibble
// first.
bool FjCDTrackStream::decodeBlock() {
	uint32 len = MIN<uint32>(_blockAlign, _dataEnd - _pos);
	if (len <= (uint32)(4 * _channels) || _stream->read(_block, len) != len)
		return false;
	_pos += len;

	ImaState st[2];
	for (int c = 0; c < _channels; c++) {
		st[c].sample = (int16)READ_LE_UINT16(_block + 4 * c);
		st[c].index = CLIP<int>(_block[4 * c + 2], 0, 88);
		_pcm[c] = (int16)st[c].sample;
	}
	int n = 1;
	const byte *p = _block + 4 * _channels;
	const byte *end = _block + len;
	if (_channels == 1) {
		for (; p < end; p++, n += 2) {
			_pcm[n] = imaDecode(st[0], *p & 0x0F);
			_pcm[n + 1] = imaDecode(st[0], *p >> 4);
		}
	} else {
		for (; p + 8 <= end; p += 8, n += 8) {
			for (int c = 0; c < 2; c++) {
				const byte *q = p + 4 * c;
				for (int i = 0; i < 4; i++) {
					_pcm[(n + 2 * i) * 2 + c] = imaDecode(st[c], q[i] & 0x0F);
					_pcm[(n + 2 * i + 1) * 2 + c] = imaDecode(st[c], q[i] >> 4);
				}
			}
		}
	}
	_pcmLen = n * _channels;
	_pcmPos = 0;
	return true;
}

int FjCDTrackStream::readBuffer(int16 *buffer, const int numSamples) {
	int samples = 0;
	while (samples < numSamples && !_done) {
		if (_pcmPos >= _pcmLen && !decodeBlock()) {
			_done = true;
			break;
		}
		int n = MIN(numSamples - samples, _pcmLen - _pcmPos);
		if (_framesLeft) {
			n = MIN<uint32>(n, _framesLeft * _channels);
			_framesLeft -= n / _channels;
			if (!_framesLeft)
				_done = true;
		}
		memcpy(buffer + samples, _pcm + _pcmPos, n * sizeof(int16));
		samples += n;
		_pcmPos += n;
	}
	return samples;
}

Audio::AudioStream *makeFjCDTrackStream(Common::SeekableReadStream *stream, int duration) {
	// RIFF header, then chunks; we need "fmt " and "data".
	byte hdr[12];
	if (stream->read(hdr, 12) != 12 || READ_BE_UINT32(hdr) != MKTAG('R','I','F','F') ||
	    READ_BE_UINT32(hdr + 8) != MKTAG('W','A','V','E')) {
		delete stream;
		return 0;
	}
	int format = 0, channels = 0, rate = 0, blockAlign = 0;
	while (!stream->eos()) {
		byte ck[8];
		if (stream->read(ck, 8) != 8)
			break;
		uint32 tag = READ_BE_UINT32(ck), size = READ_LE_UINT32(ck + 4);
		uint32 next = stream->pos() + size + (size & 1);
		if (tag == MKTAG('f','m','t',' ') && size >= 16) {
			byte fmt[16];
			stream->read(fmt, 16);
			format = READ_LE_UINT16(fmt);
			channels = READ_LE_UINT16(fmt + 2);
			rate = READ_LE_UINT32(fmt + 4);
			blockAlign = READ_LE_UINT16(fmt + 12);
		} else if (tag == MKTAG('d','a','t','a')) {
			if (format != 0x11 || channels < 1 || channels > 2 || blockAlign <= 4 * channels ||
			    (channels == 2 && (blockAlign - 8) % 8)) {
				warning("CD track: want IMA ADPCM WAV, mono or stereo (format %d, %d ch)", format, channels);
				break;
			}
			uint32 start = stream->pos();
			uint32 end = MIN<uint32>(start + size, stream->size());
			// duration is in CD frames (1/75 s); 0 plays to the end.
			uint32 maxFrames = duration > 0 ? (uint32)((uint64)duration * rate / 75) : 0;
			return new FjCDTrackStream(stream, channels, rate, blockAlign, start, end, maxFrames);
		}
		stream->seek(next, SEEK_SET);
	}
	delete stream;
	return 0;
}

} // End of namespace Scumm
