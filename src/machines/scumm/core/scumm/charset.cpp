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


#include "charset.h"
#include "scumm.h"
#include "scumm_v3.h"
#include "util.h"

namespace Scumm {

// fruitjam-scumm: no CJK fonts; _useCJKMode is always false.
byte *ScummEngine::get2byteCharPtr(int idx) {
	return _2byteFontPtr;
}

/*
TODO:
Right now our charset renderers directly access _textSurface, as well as the
virtual screens of ScummEngine. Ideally, this would not be the case. Instead,
ScummVM would simply pass the appropriate Surface to the resp. methods.
Of course it is not quite as simple, various flags and offsets have to
be taken into account for that.

The advantage will be cleaner coder (easier to debug, in particular), and a
better separation of the various modules.
*/



#pragma mark -


CharsetRenderer::CharsetRenderer(ScummEngine *vm) {
	_top = 0;
	_left = 0;
	_startLeft = 0;
	_right = 0;

	_color = 0;

	_center = false;
	_hasMask = false;
	_textScreenID = kMainVirtScreen;
	_blitAlso = false;
	_firstChar = false;
	_disableOffsX = false;

	_vm = vm;
	_curId = -1;
}

CharsetRenderer::~CharsetRenderer() {
}

CharsetRendererCommon::CharsetRendererCommon(ScummEngine *vm)
	: CharsetRenderer(vm), _bytesPerPixel(0), _fontHeight(0), _numChars(0) {
	_enableShadow = false;
	_shadowColor = 0;
}

void CharsetRendererCommon::setCurID(int32 id) {
	if (id == -1)
		return;

	assertRange(0, id, _vm->_numCharsets - 1, "charset");

	_curId = id;

	_fontPtr = _vm->getResourceAddress(rtCharset, id);
	if (_fontPtr == 0)
		error("CharsetRendererCommon::setCurID: charset %d not found", id);

	if (_vm->_game.version == 4)
		_fontPtr += 17;
	else
		_fontPtr += 29;

	_bytesPerPixel = _fontPtr[0];
	_fontHeight = _fontPtr[1];
	_numChars = READ_LE_UINT16(_fontPtr + 2);
}

void CharsetRendererV3::setCurID(int32 id) {
	if (id == -1)
		return;

	assertRange(0, id, _vm->_numCharsets - 1, "charset");

	_curId = id;

	_fontPtr = _vm->getResourceAddress(rtCharset, id);
	if (_fontPtr == 0)
		error("CharsetRendererCommon::setCurID: charset %d not found", id);

	_bytesPerPixel = 1;
	_numChars = _fontPtr[4];
	_fontHeight = _fontPtr[5];

	_fontPtr += 6;
	_widthTable = _fontPtr;
	_fontPtr += _numChars;
}

int CharsetRendererCommon::getFontHeight() {
	if (_vm->_useCJKMode)
		return MAX(_vm->_2byteHeight + 1, _fontHeight);
	else
		return _fontHeight;
}

// do spacing for variable width old-style font
int CharsetRendererClassic::getCharWidth(uint16 chr) {
	int spacing = 0;

 	if (_vm->_useCJKMode && chr >= 0x80)
		return _vm->_2byteWidth / 2;

	int offs = READ_LE_UINT32(_fontPtr + chr * 4 + 4);
	if (offs)
		spacing = _fontPtr[offs] + (signed char)_fontPtr[offs + 2];

	return spacing;
}

int CharsetRenderer::getStringWidth(int arg, const byte *text) {
	int pos = 0;
	int width = 1;
	int chr;
	int oldID = getCurID();
	int code = (_vm->_game.heversion >= 80) ? 127 : 64;

	while ((chr = text[pos++]) != 0) {
		if (chr == '\n' || chr == '\r' || chr == _vm->_newLineCharacter)
			break;
		if (_vm->_game.heversion >= 72) {
			if (chr == code) {
				chr = text[pos++];
				if (chr == 84 || chr == 116) {  // Strings of speech offset/size
					while (chr != code)
						chr = text[pos++];
					continue;
				}
				if (chr == 119) // 'Wait'
					break;
				if (chr == 104|| chr == 110) // 'Newline'
					break;
			}
		} else {
			if (chr == '@' && !(_vm->_game.id == GID_CMI && _vm->_language == Common::ZH_TWN))
				continue;
			if (chr == 255 || (_vm->_game.version <= 6 && chr == 254)) {
				chr = text[pos++];
				if (chr == 3)	// 'WAIT'
					break;
				if (chr == 8) { // 'Verb on next line'
					if (arg == 1)
						break;
					while (text[pos++] == ' ')
					;
					continue;
				}
				if (chr == 10 || chr == 21 || chr == 12 || chr == 13) {
					pos += 2;
					continue;
				}
				if (chr == 9 || chr == 1 || chr == 2) // 'Newline'
					break;
				if (chr == 14) {
					int set = text[pos] | (text[pos + 1] << 8);
					pos += 2;
					setCurID(set);
					continue;
				}
			}

			// Some localizations may override colors
			// See credits in Chinese COMI
			if (_vm->_game.id == GID_CMI && _vm->_language == Common::ZH_TWN &&
			    chr == '^' && pos == 1) {
				if (text[pos] == 'c') {
					pos += 4;
					chr = text[pos++];
				}
			}
		}

		if (_vm->_useCJKMode) {
			if (_vm->_game.platform == Common::kPlatformFMTowns) {
				if (checkSJISCode(chr))
					// This strange character conversion is the exact way the original does it here.
					// This is the only way to get an accurate text formatting in the MI1 intro.
					chr = (int8)text[pos++] | (chr << 8);
			} else if (chr & 0x80) {
				pos++;
				width += _vm->_2byteWidth;
				// Original keeps glyph width and character dimensions separately
				if (_vm->_language == Common::KO_KOR || _vm->_language == Common::ZH_TWN) {
					width++;
				}
				continue;
			}
		}
		width += getCharWidth(chr);
	}

	setCurID(oldID);

	return width;
}

void CharsetRenderer::addLinebreaks(int a, byte *str, int pos, int maxwidth) {
	int lastspace = -1;
	int curw = 1;
	int chr;
	int oldID = getCurID();
	int code = (_vm->_game.heversion >= 80) ? 127 : 64;

	while ((chr = str[pos++]) != 0) {
		if (_vm->_game.heversion >= 72) {
			if (chr == code) {
				chr = str[pos++];
				if (chr == 84 || chr == 116) {  // Strings of speech offset/size
					while (chr != code)
						chr = str[pos++];
					continue;
				}
				if (chr == 119) // 'Wait'
					break;
				if (chr == 110) { // 'Newline'
					curw = 1;
					continue;
				}
				if (chr == 104) // 'Don't terminate with \n'
					break;
			}
		} else {
			if (chr == '@')
				continue;
			if (chr == 255 || (_vm->_game.version <= 6 && chr == 254)) {
				chr = str[pos++];
				if (chr == 3) // 'Wait'
					break;
				if (chr == 8) { // 'Verb on next line'
					if (a == 1) {
						curw = 1;
					} else {
						while (str[pos] == ' ')
							str[pos++] = '@';
					}
					continue;
				}
				if (chr == 10 || chr == 21 || chr == 12 || chr == 13) {
					pos += 2;
					continue;
				}
				if (chr == 1) { // 'Newline'
					curw = 1;
					continue;
				}
				if (chr == 2) // 'Don't terminate with \n'
					break;
				if (chr == 14) {
					int set = str[pos] | (str[pos + 1] << 8);
					pos += 2;
					setCurID(set);
					continue;
				}
			}
		}
		if (chr == ' ')
			lastspace = pos - 1;

		if (chr == _vm->_newLineCharacter)
			lastspace = pos - 1;

		if (_vm->_useCJKMode) {
			if (_vm->_game.platform == Common::kPlatformFMTowns) {
				if (checkSJISCode(chr))
					// This strange character conversion is the exact way the original does it here.
					// This is the only way to get an accurate text formatting in the MI1 intro.
					chr = (int8)str[pos++] | (chr << 8);
				curw += getCharWidth(chr);
			} else if (chr & 0x80) {
				pos++;
				curw += _vm->_2byteWidth;
				// Original keeps glyph width and character dimensions separately
				if (_vm->_language == Common::KO_KOR || _vm->_language == Common::ZH_TWN) {
					curw++;
				}
			} else {
				curw += getCharWidth(chr);
			}
		} else {
			curw += getCharWidth(chr);
		}
		if (lastspace == -1)
			continue;
		if (curw > maxwidth) {
			str[lastspace] = 0xD;
			curw = 1;
			pos = lastspace + 1;
			lastspace = -1;
		}
	}

	setCurID(oldID);
}

int CharsetRendererV3::getCharWidth(uint16 chr) {
	int spacing = 0;

	if (_vm->_useCJKMode && (chr & 0x80))
		spacing = _vm->_2byteWidth / 2;

	if (!spacing)
		spacing = *(_widthTable + chr);

	return spacing;
}

void CharsetRendererPC::enableShadow(bool enable) {
	_shadowColor = 0;
	_enableShadow = enable;

	if (_vm->_game.version >= 7 && _vm->_language == Common::KO_KOR)
		_shadowType = kHorizontalShadowType;
	else
		_shadowType = kNormalShadowType;
}

void CharsetRendererPC::drawBits1(Graphics::Surface &dest, int x, int y, const byte *src, int drawTop, int width, int height) {
	byte *dst = (byte *)dest.getBasePtr(x, y);

	byte bits = 0;
	uint8 col = _color;
	int pitch = dest.pitch - width * dest.format.bytesPerPixel;
	byte *dst2 = dst + dest.pitch;

	for (y = 0; y < height && y + drawTop < dest.h; y++) {
		for (x = 0; x < width; x++) {
			if ((x % 8) == 0)
				bits = *src++;
			if ((bits & revBitMask(x % 8)) && y + drawTop >= 0) {
				if (_enableShadow) {
					if (_shadowType == kNormalShadowType)
						dst[1] = dst2[0] = dst2[1] = _shadowColor;
					else if (_shadowType == kHorizontalShadowType)
						dst[1] = _shadowColor;
				}
				dst[0] = col;
			}
			dst += dest.format.bytesPerPixel;
			dst2 += dest.format.bytesPerPixel;
		}

		dst += pitch;
		dst2 += pitch;
	}
}

int CharsetRendererV3::getDrawWidthIntern(uint16 chr) {
	return getCharWidth(chr);
}

int CharsetRendererV3::getDrawHeightIntern(uint16) {
	return 8;
}

void CharsetRendererV3::setColor(byte color) {
	bool useShadow = false;
	_color = color;

	// FM-TOWNS version of Loom uses old color method as well
	if ((_vm->_game.version >= 2) && ((_vm->_game.features & GF_16COLOR) || (_vm->_game.id == GID_LOOM && _vm->_game.version == 3))) {
		useShadow = ((_color & 0xF0) != 0);
		_color &= 0x0f;
	} else if (_vm->_game.features & GF_OLD256) {
		useShadow = ((_color & 0x80) != 0);
		_color &= 0x7f;
	} else
		useShadow = false;


	enableShadow(useShadow);

	translateColor();
}


void CharsetRendererV3::printChar(int chr, bool ignoreCharsetMask) {
	// WORKAROUND for bug #1509509: Indy3 Mac does not show black
	// characters (such as in the grail diary) if ignoreCharsetMask
	// is true. See also patch #1851568.
	if (_vm->_game.id == GID_INDY3 && _vm->_game.platform == Common::kPlatformMacintosh && _color == 0)
		ignoreCharsetMask = false;

	// Indy3 / Zak256 / Loom
	int width, height, origWidth = 0, origHeight;
	VirtScreen *vs;
	const byte *charPtr;
	int is2byte = (chr >= 256 && _vm->_useCJKMode) ? 1 : 0;

	assertRange(0, _curId, _vm->_numCharsets - 1, "charset");

	if ((vs = _vm->findVirtScreen(_top)) == NULL)
		return;

	if (chr == '@')
		return;

	charPtr = (_vm->_useCJKMode && chr > 127) ? _vm->get2byteCharPtr(chr) : _fontPtr + chr * 8;
	width = getDrawWidthIntern(chr);
	height = getDrawHeightIntern(chr);
	setDrawCharIntern(chr);

	origWidth = width;
	origHeight = height;

	// Clip at the right side (to avoid drawing "outside" the screen bounds).
	if (_left + origWidth > _right + 1)
		return;

	if (_enableShadow) {
		width++;
		height++;
	}

	if (_firstChar) {
		_str.left = _left;
		_str.top = _top;
		_str.right = _left;
		_str.bottom = _top;
		_firstChar = false;
	}

	int drawTop = _top - vs->topline;

	_vm->markRectAsDirty(vs->number, _left, _left + width, drawTop, drawTop + height);

	if (!ignoreCharsetMask) {
		_hasMask = true;
		_textScreenID = vs->number;
	}

	if ((ignoreCharsetMask || !vs->hasTwoBuffers)
		)
		drawBits1(*vs, _left + vs->xstart, drawTop, charPtr, drawTop, origWidth, origHeight);
	else
		drawBits1(_vm->_textSurface, _left * _vm->_textSurfaceMultiplier, _top * _vm->_textSurfaceMultiplier, charPtr, drawTop, origWidth, origHeight);

	if (is2byte) {
		origWidth /= _vm->_textSurfaceMultiplier;
		height /= _vm->_textSurfaceMultiplier;
	}

	if (_str.left > _left)
		_str.left = _left;

	_left += origWidth;

	if (_str.right < _left) {
		_str.right = _left;
		if (_enableShadow)
			_str.right++;
	}

	if (_str.bottom < _top + height)
		_str.bottom = _top + height;
}

void CharsetRendererV3::drawChar(int chr, Graphics::Surface &s, int x, int y) {
	const byte *charPtr = (_vm->_useCJKMode && chr > 127) ? _vm->get2byteCharPtr(chr) : _fontPtr + chr * 8;
	int width = getDrawWidthIntern(chr);
	int height = getDrawHeightIntern(chr);
	setDrawCharIntern(chr);
	drawBits1(s, x, y, charPtr, y, width, height);
}

void CharsetRenderer::translateColor() {
	// Based on disassembly
	if (_vm->_renderMode == Common::kRenderCGA) {
		static const byte CGAtextColorMap[16] = {0,  3, 3, 3, 5, 5, 5,  15,
										   15, 3, 3, 3, 5, 5, 15, 15};
		_color = CGAtextColorMap[_color & 0x0f];
	}

	if (_vm->_renderMode == Common::kRenderHercA || _vm->_renderMode == Common::kRenderHercG) {
		static const byte HercTextColorMap[16] = {0, 15,  2, 15, 15,  5, 15,  15,
										   8, 15, 15, 15, 15, 15, 15, 15};
		_color = HercTextColorMap[_color & 0x0f];
	}
}

void CharsetRenderer::saveLoadWithSerializer(Common::Serializer &ser) {
	ser.syncAsByte(_curId, VER(73), VER(73));
	ser.syncAsSint32LE(_curId, VER(74));
	ser.syncAsByte(_color, VER(73));

	if (ser.isLoading()) {
		setCurID(_curId);
		setColor(_color);
	}
}

void CharsetRendererClassic::printChar(int chr, bool ignoreCharsetMask) {
	VirtScreen *vs;
	bool is2byte = (chr >= 256 && _vm->_useCJKMode);

	assertRange(1, _curId, _vm->_numCharsets - 1, "charset");

	if ((vs = _vm->findVirtScreen(_top)) == NULL && (vs = _vm->findVirtScreen(_top + getFontHeight())) == NULL)
		return;

	if (chr == '@')
		return;

	translateColor();

	_vm->_charsetColorMap[1] = _color;

	if (!prepareDraw(chr))
		return;

	if (_firstChar) {
		_str.left = 0;
		_str.top = 0;
		_str.right = 0;
		_str.bottom = 0;
	}

	_top += _offsY;
	_left += _offsX;

	if (_left + _origWidth > _right + 1 || _left < 0) {
		_left += _origWidth;
		_top -= _offsY;
		return;
	}

	_disableOffsX = false;

	if (_firstChar) {
		_str.left = _left;
		_str.top = _top;
		_str.right = _left;
		_str.bottom = _top;
		_firstChar = false;
	}

	if (_left < _str.left)
		_str.left = _left;

	if (_top < _str.top)
		_str.top = _top;

	int drawTop = _top - vs->topline;

	_vm->markRectAsDirty(vs->number, _left, _left + _width, drawTop, drawTop + _height);

	// This check for kPlatformFMTowns and kMainVirtScreen is at least required for the chat with
	// the navigator's head in front of the ghost ship in Monkey Island 1
	if (!ignoreCharsetMask || (_vm->_game.platform == Common::kPlatformFMTowns && vs->number == kMainVirtScreen)) {
		_hasMask = true;
		_textScreenID = vs->number;
	}

	// We need to know the virtual screen we draw on for Indy 4 Amiga, since
	// it selects the palette map according to this. We furthermore can not
	// use _textScreenID here, since that will cause inventory graphics
	// glitches.
	if (_vm->_game.platform == Common::kPlatformAmiga && _vm->_game.id == GID_INDY4)
		_drawScreen = vs->number;

	printCharIntern(is2byte, _charPtr, _origWidth, _origHeight, _width, _height, vs, ignoreCharsetMask);

	// Original keeps glyph width and character dimensions separately
	if ((_vm->_language == Common::ZH_TWN || _vm->_language == Common::KO_KOR) && is2byte)
		_origWidth++;

	_left += _origWidth;

	if (_str.right < _left) {
		_str.right = _left;
		if (_vm->_game.platform != Common::kPlatformFMTowns && _enableShadow)
			_str.right++;
	}

	if (_str.bottom < _top + _origHeight)
		_str.bottom = _top + _origHeight;

	_top -= _offsY;
}

void CharsetRendererClassic::printCharIntern(bool is2byte, const byte *charPtr, int origWidth, int origHeight, int width, int height, VirtScreen *vs, bool ignoreCharsetMask) {
	byte *dstPtr;
	byte *back = NULL;
	int drawTop = _top - vs->topline;

	if ((_vm->_game.heversion >= 71 && _bytesPerPixel >= 8) || (_vm->_game.heversion >= 90 && _bytesPerPixel == 0)) {
	} else {
		Graphics::Surface dstSurface;
		Graphics::Surface backSurface;
		if ((ignoreCharsetMask || !vs->hasTwoBuffers)) {
			dstSurface = *vs;
			dstPtr = vs->getPixels(_left, drawTop);
		} else {
			dstSurface = _vm->_textSurface;
			dstPtr = (byte *)_vm->_textSurface.getBasePtr(_left * _vm->_textSurfaceMultiplier, (_top - _vm->_screenTop) * _vm->_textSurfaceMultiplier);
		}

		if (_blitAlso && vs->hasTwoBuffers) {
			backSurface = dstSurface;
			back = dstPtr;
			dstSurface = *vs;
			dstPtr = vs->getBackPixels(_left, drawTop);
		}

		if (!ignoreCharsetMask && vs->hasTwoBuffers) {
			drawTop = _top - _vm->_screenTop;
		}

		if (is2byte && _vm->_game.platform != Common::kPlatformFMTowns)
			drawBits1(dstSurface, _left, drawTop, charPtr, drawTop, origWidth, origHeight);
		else
			drawBitsN(dstSurface, dstPtr, charPtr, *_fontPtr, drawTop, origWidth, origHeight);

		if (_blitAlso && vs->hasTwoBuffers) {
			// FIXME: Revisiting this code, I think the _blitAlso mode is likely broken
			// right now -- we are copying stuff from "dstPtr" to "back", but "dstPtr" really
			// only conatains charset data...
			// One way to fix this: don't copy etc.; rather simply render the char twice,
			// once to each of the two buffers. That should hypothetically yield
			// identical results, though I didn't try it and right now I don't know
			// any spots where I can test this...
			if (!ignoreCharsetMask)
				error("This might be broken -- please report where you encountered this to Fingolfin");

			// Perform some clipping
			int w = MIN(width, dstSurface.w - _left);
			int h = MIN(height, dstSurface.h - drawTop);
			if (_left < 0) {
				w += _left;
				back -= _left;
				dstPtr -= _left;
			}
			if (drawTop < 0) {
				h += drawTop;
				back -= drawTop * backSurface.pitch;
				dstPtr -= drawTop * dstSurface.pitch;
			}

			// Blit the image data
			if (w > 0) {
				while (h-- > 0) {
					memcpy(back, dstPtr, w);
					back += backSurface.pitch;
					dstPtr += dstSurface.pitch;
				}
			}
		}
	}
}

bool CharsetRendererClassic::prepareDraw(uint16 chr) {
	bool is2byte = (chr >= 256 && _vm->_useCJKMode);
	if (is2byte) {
		if (_vm->_language == Common::KO_KOR)
			enableShadow(true);

		_charPtr = _vm->get2byteCharPtr(chr);
		_width = _origWidth = _vm->_2byteWidth;
		_height = _origHeight = _vm->_2byteHeight;
		_offsX = _offsY = 0;

		if (_enableShadow) {
			_width++;
			_height++;
		}

		return true;
	}

	uint32 charOffs = READ_LE_UINT32(_fontPtr + chr * 4 + 4);
	assert(charOffs < 0x14000);
	if (!charOffs)
		return false;
	_charPtr = _fontPtr + charOffs;

	_width = _origWidth = _charPtr[0];
	_height = _origHeight = _charPtr[1];

	if (_disableOffsX) {
		_offsX = 0;
	} else {
		_offsX = (signed char)_charPtr[2];
	}

	_offsY = (signed char)_charPtr[3];

	_charPtr += 4;	// Skip over char header
	return true;
}

void CharsetRendererClassic::drawChar(int chr, Graphics::Surface &s, int x, int y) {
	if (!prepareDraw(chr))
		return;

	byte *dst = (byte *)s.getBasePtr(x, y);

	bool is2byte = (_vm->_useCJKMode && chr >= 256);
	if (is2byte)
		drawBits1(s, x, y, _charPtr, y, _width, _height);
	else
		drawBitsN(s, dst, _charPtr, *_fontPtr, y, _width, _height);
}

void CharsetRendererClassic::drawBitsN(const Graphics::Surface &s, byte *dst, const byte *src, byte bpp, int drawTop, int width, int height) {
	int y, x;
	int color;
	byte numbits, bits;

	int pitch = s.pitch - width;

	assert(bpp == 1 || bpp == 2 || bpp == 4 || bpp == 8);
	bits = *src++;
	numbits = 8;
	byte *cmap = _vm->_charsetColorMap;

	// Indy4 Amiga always uses the room or verb palette map to match colors to
	// the currently setup palette, thus we need to select it over here too.
	// Done like the original interpreter.
	byte *amigaMap = 0;
	if (_vm->_game.platform == Common::kPlatformAmiga && _vm->_game.id == GID_INDY4) {
		if (_drawScreen == kVerbVirtScreen)
			amigaMap = _vm->_verbPalette;
		else
			amigaMap = _vm->_roomPalette;
	}

	for (y = 0; y < height && y + drawTop < s.h; y++) {
		for (x = 0; x < width; x++) {
			color = (bits >> (8 - bpp)) & 0xFF;

			if (color && y + drawTop >= 0) {
				if (amigaMap)
					*dst = amigaMap[cmap[color]];
				else
					*dst = cmap[color];
			}
			dst++;
			bits <<= bpp;
			numbits -= bpp;
			if (numbits == 0) {
				bits = *src++;
				numbits = 8;
			}
		}
		dst += pitch;
	}
}




// fruitjam-scumm: from upstream charset.cpp, without the SJIS path.
void CharsetRendererPCE::setColor(byte color) {
	_vm->setPCETextPalette(color);
	_color = 15;

	enableShadow(true);
}

void CharsetRendererPCE::drawBits1(Graphics::Surface &dest, int x, int y, const byte *src, int drawTop, int width, int height) {
	byte *dst = (byte *)dest.getBasePtr(x, y);
	byte bits = 0;

	for (y = 0; y < height && y + drawTop < dest.h; y++) {
		int bitCount = 0;
		for (x = 0; x < width; x++) {
			if ((bitCount % 8) == 0)
				bits = *src++;
			if ((bits & revBitMask(bitCount % 8)) && y + drawTop >= 0) {
				if (dest.format.bytesPerPixel == 2) {
					if (_enableShadow)
						WRITE_UINT16(dst + dest.pitch + 2, _vm->_16BitPalette[_shadowColor]);
					WRITE_UINT16(dst, _vm->_16BitPalette[_color]);
				} else {
					if (_enableShadow)
						*(dst + dest.pitch + 1) = _shadowColor;
					*dst = _color;
				}
			}
			dst += dest.format.bytesPerPixel;
			bitCount++;
		}

		dst += dest.pitch - width * dest.format.bytesPerPixel;
	}
}

} // End of namespace Scumm
