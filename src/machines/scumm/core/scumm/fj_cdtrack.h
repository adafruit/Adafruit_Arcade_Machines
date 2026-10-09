/* fruitjam-scumm - SCUMM v3/v4 on the Adafruit Fruit Jam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef SCUMM_FJ_CDTRACK_H
#define SCUMM_FJ_CDTRACK_H

namespace Common {
class SeekableReadStream;
}

namespace Audio {
class AudioStream;
}

namespace Scumm {

/**
 * A CD audio track converted to an IMA ADPCM WAV file (mono or stereo).
 * Plays duration CD frames (1/75 s), or the whole track if duration is 0.
 * Takes ownership of stream, also on failure. Returns NULL if the file is
 * not an IMA ADPCM WAV.
 */
Audio::AudioStream *makeFjCDTrackStream(Common::SeekableReadStream *stream, int duration);

} // End of namespace Scumm

#endif
