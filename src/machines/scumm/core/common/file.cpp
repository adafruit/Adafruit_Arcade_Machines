#include "../backend/fj_arduino.h" // Adafruit Arcade Machines build mode
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

#include "file.h"
#include "textconsole.h"

namespace Common {

File::File() : _handle(0) {
}

File::~File() {
	close();
}

bool File::open(const String &filename) {
	assert(!filename.empty());
	assert(!_handle);
	_handle = fj_open_read(filename.c_str());
	if (_handle)
		_name = filename;
	return _handle != 0;
}

bool File::open(SeekableReadStream *stream, const String &name) {
	assert(!_handle);
	if (stream) {
		_handle = stream;
		_name = name;
	}
	return _handle != 0;
}

bool File::exists(const String &filename) {
	return fj_file_exists(filename.c_str());
}

void File::close() {
	delete _handle;
	_handle = 0;
}

bool File::isOpen() const {
	return _handle != 0;
}

bool File::err() const {
	assert(_handle);
	return _handle->err();
}

void File::clearErr() {
	assert(_handle);
	_handle->clearErr();
}

bool File::eos() const {
	assert(_handle);
	return _handle->eos();
}

int32 File::pos() const {
	assert(_handle);
	return _handle->pos();
}

int32 File::size() const {
	assert(_handle);
	return _handle->size();
}

bool File::seek(int32 offs, int whence) {
	assert(_handle);
	return _handle->seek(offs, whence);
}

uint32 File::read(void *ptr, uint32 len) {
	assert(_handle);
	return _handle->read(ptr, len);
}

} // End of namespace Common
