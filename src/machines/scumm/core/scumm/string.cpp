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
#include "../audio/mixer.h"

#include "actor.h"
#include "charset.h"
#include "file.h"
#include "resource.h"
#include "scumm.h"
#include "scumm_v3.h"
#include "verbs.h"
#include "sound.h"

namespace Scumm {



#pragma mark -
#pragma mark --- "High level" message code ---
#pragma mark -


void ScummEngine::printString(int m, const byte *msg) {
	switch (m) {
	case 0:
		actorTalk(msg);
		break;
	case 1:
		drawString(1, msg);
		break;
	case 2:
		debugMessage(msg);
		break;
	case 3:
		showMessageDialog(msg);
		break;
	default:
		break;
	}
}


void ScummEngine::debugMessage(const byte *msg) {
	byte buffer[500];
	convertMessageToString(msg, buffer, sizeof(buffer));

	if ((buffer[0] != 0xFF) && _debugMode) {
		debug(0, "DEBUG: %s", buffer);
		return;
	}

	if (buffer[0] == 0xFF && buffer[1] == 10) {
		uint32 a, b;
		int channel = 0;

		a = buffer[2] | (buffer[3] << 8) | (buffer[6] << 16) | (buffer[7] << 24);
		b = buffer[10] | (buffer[11] << 8) | (buffer[14] << 16) | (buffer[15] << 24);

		// Sam and Max uses a caching system, printing empty messages
		// and setting VAR_V6_SOUNDMODE beforehand. See patch 609791.
		if (_game.id == GID_SAMNMAX)
			channel = VAR(VAR_V6_SOUNDMODE);

		if (channel != 2)
			_sound->talkSound(a, b, 1, channel);
	}
}

void ScummEngine::showMessageDialog(const byte *msg) {
	// Original COMI used different code at this point.
	// Seemed to use blastText for the messages
	byte buf[500];

	convertMessageToString(msg, buf, sizeof(buf));

	if (_string[3].color == 0)
		_string[3].color = 4;

	// fruitjam-scumm: no dialogs; log it and carry on as if Enter was pressed.
	warning("message: %s", (char *)buf);
	VAR(VAR_KEYPRESS) = 13;
}


#pragma mark -
#pragma mark --- V6 blast text queue code ---
#pragma mark -



#pragma mark -
#pragma mark --- V7 subtitle queue code ---
#pragma mark -





#pragma mark -
#pragma mark --- Core message/subtitle code ---
#pragma mark -


bool ScummEngine::handleNextCharsetCode(Actor *a, int *code) {
	uint32 talk_sound_a = 0;
	uint32 talk_sound_b = 0;
	int color, frme, c = 0, oldy;
	bool endLoop = false;
	byte *buffer = _charsetBuffer + _charsetBufPos;
	while (!endLoop) {
		c = *buffer++;
		if (!(c == 0xFF || (_game.version <= 6 && c == 0xFE))) {
			break;
		}
		c = *buffer++;

		if (_newLineCharacter != 0 && c == _newLineCharacter) {
			c = 13;
			break;
		}

		switch (c) {
		case 1:
			c = 13; // new line
			_msgCount = _screenWidth;
			endLoop = true;
			break;
		case 2:
			_haveMsg = 0;
			_keepText = true;
			endLoop = true;
			break;
		case 3:
			_haveMsg = (_game.version >= 7) ? 1 : 0xFF;
			_keepText = false;
			_msgCount = 0;
			endLoop = true;
			break;
		case 8:
			// Ignore this code here. Occurs e.g. in MI2 when you
			// talk to the carpenter on scabb island. It works like
			// code 1 (=newline) in verb texts, but is ignored in
			// spoken text (i.e. here). Used for very long verb
			// sentences.
			break;
		case 9:
			frme = buffer[0] | (buffer[1] << 8);
			buffer += 2;
			if (a)
				a->startAnimActor(frme);
			break;
		case 10:
			// Note the similarity to the code in debugMessage()
			talk_sound_a = buffer[0] | (buffer[1] << 8) | (buffer[4] << 16) | (buffer[5] << 24);
			talk_sound_b = buffer[8] | (buffer[9] << 8) | (buffer[12] << 16) | (buffer[13] << 24);
			buffer += 14;
			_sound->talkSound(talk_sound_a, talk_sound_b, 2);
			_haveActorSpeechMsg = false;
			break;
		case 12:
			color = buffer[0] | (buffer[1] << 8);
			buffer += 2;
			if (color == 0xFF)
				_charset->setColor(_charsetColor);
			else
				_charset->setColor(color);
			break;
		case 13:
			debug(0, "handleNextCharsetCode: Unknown opcode 13 %d", READ_LE_UINT16(buffer));
			buffer += 2;
			break;
		case 14:
			oldy = _charset->getFontHeight();
			_charset->setCurID(*buffer++);
			buffer += 2;
			memcpy(_charsetColorMap, _charsetData[_charset->getCurID()], 4);
			_nextTop -= _charset->getFontHeight() - oldy;
			break;
		default:
			error("handleNextCharsetCode: invalid code %d", c);
		}
	}
	_charsetBufPos = buffer - _charsetBuffer;
	*code = c;
	return (c != 2 && c != 3);
}


bool ScummEngine::newLine() {
	_nextLeft = _string[0].xpos;
	if (_charset->_center) {
		_nextLeft -= _charset->getStringWidth(0, _charsetBuffer + _charsetBufPos) / 2;
		if (_nextLeft < 0)
			_nextLeft = _game.version >= 6 ? _string[0].xpos : 0;
	} else if (_game.version >= 4 && _game.version < 7 && _game.heversion == 0 && _language == Common::HE_ISR) {
		if (_game.id == GID_MONKEY && _charset->getCurID() == 4) {
			_nextLeft = _screenWidth - _charset->getStringWidth(0, _charsetBuffer + _charsetBufPos) - _nextLeft;
		}
	}
	if (_game.version == 0) {
		return false;
	} else if (!(_game.platform == Common::kPlatformFMTowns) && _string[0].height) {
		_nextTop += _string[0].height;
	} else {
		bool useCJK = _useCJKMode;
		// SCUMM5 FM-Towns doesn't use the height of the ROM font here.
		if (_game.platform == Common::kPlatformFMTowns && _game.version == 5)
			_useCJKMode = false;
		_nextTop += _charset->getFontHeight();
		_useCJKMode = useCJK;
	}
	if (_game.version > 3) {
		// FIXME: is this really needed?
		_charset->_disableOffsX = true;
	}
	return true;
}

void ScummEngine::fakeBidiString(byte *ltext, bool ignoreVerb) {
	// Provides custom made BiDi mechanism.
	// Reverses texts on each line marked by control characters (considering different control characters used in verbs panel)
	// While preserving original order of numbers (also negative numbers and comma separated)
	int32 ll = 0;
	if (_game.id == GID_INDY4 && ltext[ll] == 0x7F) {
		ll++;
	}
	while (ltext[ll] == 0xFF) {
		ll += 4;
	}
	int32 ipos = 0;
	int32 start = 0;
	byte *text = ltext + ll;
	byte *current = text;

	int32 bufferSize = 384;
	byte * const buff = (byte *)calloc(sizeof(byte), bufferSize);
	assert(buff);
	byte * const stack = (byte *)calloc(sizeof(byte), bufferSize);
	assert(stack);

	while (1) {
		if (*current == 0x0D || *current == 0 || *current == 0xFF || *current == 0xFE) {

			// ignore the line break for verbs texts
			if (ignoreVerb && (*(current + 1) ==  8)) {
				*(current + 1) = *current;
				*current = 0x08;
				ipos += 2;
				current += 2;
				continue;
			}

			memset(buff, 0, bufferSize);
			memset(stack, 0, bufferSize);

			// Reverse string on current line (between start and ipos).
			int32 sthead = 0;
			byte last = 0;
			for (int32 j = 0; j < ipos; j++) {
				byte *curr = text + start + ipos - j - 1;
				// Special cases to preserve original ordering (numbers).
				if (Common::isDigit(*curr) ||
						(*curr == (byte)',' && j + 1 < ipos && Common::isDigit(*(curr - 1)) && Common::isDigit(last)) ||
						(*curr == (byte)'-' && (j + 1 == ipos || Common::isSpace(*(curr - 1))) && Common::isDigit(last))) {
					++sthead;
					stack[sthead] = *curr;
				} else {
					while (sthead > 0) {
						buff[j - sthead] = stack[sthead];
						--sthead;
					}
					buff[j] = *curr;
				}
				last = *curr;
			}
			while (sthead > 0) {
				buff[ipos - sthead] = stack[sthead];
				--sthead;
			}
			memcpy(text + start, buff, ipos);
			start += ipos + 1;
			ipos = -1;
			if (*current == 0xFF || *current == 0xFE) {
				current++;
				if (*current == 0x03 || *current == 0x02) {
					break;
				}
				if (*current == 0x0A || *current == 0x0C) {
					start += 2;
					current += 2;
				}
				start++;
				ipos++;
				current++;
				continue;
			}
		}
		if (*current) {
			ipos++;
			current++;
			continue;
		}
		break;
	}
	if (!ignoreVerb && _game.id == GID_INDY4 && ltext[0] == 0x7F) {
		ltext[start + ipos + ll] = 0x80;
		ltext[start + ipos + ll + 1] = 0;
	}

	free(buff);
	free(stack);
}

void ScummEngine::CHARSET_1() {
	Actor *a;

	if (_game.heversion >= 70 && _haveMsg == 3) {
		stopTalk();
		return;
	}

	if (!_haveMsg)
		return;

	if (_game.version >= 4 && _game.version <= 6) {
		// Do nothing while the camera is moving
		if ((camera._dest.x / 8) != (camera._cur.x / 8) || camera._cur.x != camera._last.x)
			return;
	}

	a = NULL;
	if (getTalkingActor() != 0xFF)
		a = derefActorSafe(getTalkingActor(), "CHARSET_1");

	if (a && _string[0].overhead) {
		int s;

		_string[0].xpos = a->getPos().x - _virtscr[kMainVirtScreen].xstart;
		_string[0].ypos = a->getPos().y - a->getElevation() - _screenTop;

		if (_game.version <= 5) {

			if (VAR(VAR_V5_TALK_STRING_Y) < 0) {
				s = (a->_scaley * (int)VAR(VAR_V5_TALK_STRING_Y)) / 0xFF;
				_string[0].ypos += (int)(((VAR(VAR_V5_TALK_STRING_Y) - s) / 2) + s);
			} else {
				_string[0].ypos = (int)VAR(VAR_V5_TALK_STRING_Y);
			}

		} else {
			s = a->_scalex * a->_talkPosX / 0xFF;
			_string[0].xpos += ((a->_talkPosX - s) / 2) + s;

			s = a->_scaley * a->_talkPosY / 0xFF;
			_string[0].ypos += ((a->_talkPosY - s) / 2) + s;

			if (_string[0].ypos > _screenHeight - 40)
				_string[0].ypos = _screenHeight - 40;
		}

		if (_string[0].ypos < 1)
			_string[0].ypos = 1;

		if (_string[0].xpos < 80)
			_string[0].xpos = 80;
		if (_string[0].xpos > _screenWidth - 80)
			_string[0].xpos = _screenWidth - 80;
	}

	_charset->_top = _string[0].ypos + _screenTop;
	_charset->_startLeft = _charset->_left = _string[0].xpos;
	_charset->_right = _string[0].right;
	_charset->_center = _string[0].center;
	_charset->setColor(_charsetColor);

	if (a && a->_charset)
		_charset->setCurID(a->_charset);
	else
		_charset->setCurID(_string[0].charset);

	if (_game.version >= 5)
		memcpy(_charsetColorMap, _charsetData[_charset->getCurID()], 4);


	if (_talkDelay)
		return;

	if ((_game.version <= 6 && _haveMsg == 1) ||
	    (_game.version == 7 && _haveMsg != 1)) {

		if (_game.heversion >= 60) {
			if (_sound->isSoundRunning(1) == 0)
				stopTalk();
		} else {
			if ((_sound->_sfxMode & 2) == 0)
				stopTalk();
		}
		return;
	}

	if (a && !_string[0].no_talk_anim) {
		a->runActorTalkScript(a->_talkStartFrame);
		_useTalkAnims = true;
	}

	_talkDelay = (VAR_DEFAULT_TALK_DELAY != 0xFF) ? VAR(VAR_DEFAULT_TALK_DELAY) : 60;

	if (!_keepText) {
		if (_game.version >= 7) {
		} else {
				restoreCharsetBg();
		}
		_msgCount = 0;
	} else if (_game.version <= 2) {
		_talkDelay += _msgCount * _defaultTalkDelay;
	}

	if (_game.version > 3) {
		int maxwidth = _charset->_right - _string[0].xpos - 1;
		if (_charset->_center) {
			if (maxwidth > _nextLeft)
				maxwidth = _nextLeft;
			maxwidth *= 2;
		}

		_charset->addLinebreaks(0, _charsetBuffer + _charsetBufPos, 0, maxwidth);
	}

	if (_charset->_center) {
		_nextLeft -= _charset->getStringWidth(0, _charsetBuffer + _charsetBufPos) / 2;
		if (_nextLeft < 0)
			_nextLeft = _game.version >= 6 ? _string[0].xpos : 0;
	} else if (_game.version >= 4 && _game.version < 7 && _game.heversion == 0 && _language == Common::HE_ISR) {
		if (_game.id == GID_MONKEY && _charset->getCurID() == 4) {
			_nextLeft = _screenWidth - _charset->getStringWidth(0, _charsetBuffer + _charsetBufPos) - _nextLeft;
		}
	}

	_charset->_disableOffsX = _charset->_firstChar = !_keepText;

	int c = 0;

	if (_game.version >= 4 && _game.version < 7 && _game.heversion == 0 && _language == Common::HE_ISR) {
		fakeBidiString(_charsetBuffer + _charsetBufPos, true);
	}

	while (handleNextCharsetCode(a, &c)) {
		if (c == 0) {
			// End of text reached, set _haveMsg accordingly
			_haveMsg = (_game.version >= 7) ? 2 : 1;
			_keepText = false;
			_msgCount = 0;
			break;
		}

		if (c == 13) {
			if (!newLine())
				break;
			continue;
		}

		// Handle line overflow for V3. See also bug #1306269.
		if (_game.version == 3 && _nextLeft >= _screenWidth) {
			_nextLeft = _screenWidth;
		}
		// Handle line breaks for V1-V2
		if (_game.version <= 2 && _nextLeft >= _screenWidth) {
			if (!newLine())
				break;	// FIXME: Is this necessary? Only would be relevant for v0 games
		}

		_charset->_left = _nextLeft;
		_charset->_top = _nextTop;

		if (_game.version >= 7) {
		} else {
			if (c & 0x80 && _useCJKMode) {
				if (checkSJISCode(c)) {
					byte *buffer = _charsetBuffer + _charsetBufPos;
					c += *buffer++ * 256; //LE
					_charsetBufPos = buffer - _charsetBuffer;
				}
			}
			if (_game.version <= 3) {
				_charset->printChar(c, false);
				_msgCount += 1;
			} else {
				if (_game.features & GF_16BIT_COLOR) {
					// HE games which use sprites for subtitles
				} else if (_game.heversion >= 60 && !ConfMan.getBool("subtitles") && _sound->isSoundRunning(1)) {
					// Special case for HE games
				} else if (_game.id == GID_LOOM && !ConfMan.getBool("subtitles") && (_sound->pollCD())) {
					// Special case for Loom (CD), since it only uses CD audio.for sound
				} else if (!ConfMan.getBool("subtitles") && (!_haveActorSpeechMsg || _mixer->isSoundHandleActive(*_sound->_talkChannelHandle))) {
					// Subtitles are turned off, and there is a voice version
					// of this message -> don't print it.
				} else {
					_charset->printChar(c, false);
				}
			}
			_nextLeft = _charset->_left;
			_nextTop = _charset->_top;
		}

		if (_game.version <= 2) {
			_talkDelay += _defaultTalkDelay;
			VAR(VAR_CHARCOUNT)++;
		} else {
			_talkDelay += (int)VAR(VAR_CHARINC);
		}
	}


}


void ScummEngine::drawString(int a, const byte *msg) {
	byte buf[270];
	byte *space;
	int i, c;
	byte fontHeight = 0;
	uint color;
	int code = (_game.heversion >= 80) ? 127 : 64;

	// drawString is not used in SCUMM v7 and v8
	assert(_game.version < 7);

	convertMessageToString(msg, buf, sizeof(buf));

	if (_game.version >= 4 && _game.version < 7 && _game.heversion == 0 && _language == Common::HE_ISR) {
		fakeBidiString(buf, false);
	}

	_charset->_top = _string[a].ypos + _screenTop;
	_charset->_startLeft = _charset->_left = _string[a].xpos;
	_charset->_right = _string[a].right;
	_charset->_center = _string[a].center;
	_charset->setColor(_string[a].color);
	_charset->_disableOffsX = _charset->_firstChar = true;
	_charset->setCurID(_string[a].charset);

	// HACK: Correct positions of text in books in Indy3 Mac.
	// See also patch #1851568.
	if (_game.id == GID_INDY3 && _game.platform == Common::kPlatformMacintosh && a == 1) {
		if (_currentRoom == 75) {
			// Grail Diary Page 1 (Library)
			if (_charset->_startLeft < 160)
				_charset->_startLeft = _charset->_left = _string[a].xpos - 22;
			else if (_charset->_startLeft < 200)
				_charset->_startLeft = _charset->_left = _string[a].xpos - 10;
		} else if (_currentRoom == 90) {
			// Grail Diary Page 2 (Catacombs - Engravings)
			if (_charset->_startLeft < 160)
				_charset->_startLeft = _charset->_left = _string[a].xpos - 21;
			else if (_charset->_startLeft < 200)
				_charset->_startLeft = _charset->_left = _string[a].xpos - 15;
		} else if (_currentRoom == 69) {
			// Grail Diary Page 3 (Catacombs - Music)
			if (_charset->_startLeft < 160)
				_charset->_startLeft = _charset->_left = _string[a].xpos - 15;
			else if (_charset->_startLeft < 200)
				_charset->_startLeft = _charset->_left = _string[a].xpos - 10;
		} else if (_currentRoom == 74) {
			// Biplane Manual
			_charset->_startLeft = _charset->_left = _string[a].xpos - 35;
		}
	}

	if (_game.version >= 5)
		memcpy(_charsetColorMap, _charsetData[_charset->getCurID()], 4);

	fontHeight = _charset->getFontHeight();

	if (_game.version >= 4) {
		// trim from the right
		byte *tmp = buf;
		space = NULL;
		while (*tmp) {
			if (*tmp == ' ') {
				if (!space)
					space = tmp;
			} else {
				space = NULL;
			}
			tmp++;
		}
		if (space)
			*space = '\0';
	}

	if (_charset->_center) {
		_charset->_left -= _charset->getStringWidth(a, buf) / 2;
	} else if (_game.version >= 4 && _game.version < 7 && _game.heversion == 0 && _game.id != GID_SAMNMAX && _language == Common::HE_ISR) {
		// Ignore INDY4 verbs (but allow dialogue)
		if (_game.id != GID_INDY4 || buf[0] == 127) {
			int ll = 0;
			if (_game.id == GID_INDY4 && buf[0] == 127) {
				buf[0] = 32;
				ll++;
			}

			// Skip control characters as they might contain '\0' which results in incorrect string width.
			byte *ltext = buf;
			while (ltext[ll] == 0xFF) {
				ll += 4;
			}
			byte lenbuf[270];
			memset(lenbuf, 0, sizeof(lenbuf));
			int pos = ll;
			while (ltext[pos]) {
				if ((ltext[pos] == 0xFF || (_game.version <= 6 && ltext[pos] == 0xFE)) && ltext[pos+1] == 8) {
					break;
				}
				pos++;
			}
			memcpy(lenbuf, ltext, pos);
			_charset->_left = _screenWidth - _charset->_startLeft - _charset->getStringWidth(a, lenbuf);
		}
	}

	if (!buf[0]) {
		if (_game.version >= 5) {
			buf[0] = ' ';
			buf[1] = 0;
		} else {
			_charset->_str.left = _charset->_left;
			_charset->_str.top = _charset->_top;
			_charset->_str.right = _charset->_left;
			_charset->_str.bottom = _charset->_top;
		}
	}

	for (i = 0; (c = buf[i++]) != 0;) {
		if (_game.heversion >= 72 && c == code) {
			c = buf[i++];
			switch (c) {
			case 110:
				if (_charset->_center) {
					_charset->_left = _charset->_startLeft - _charset->getStringWidth(a, buf + i);
				} else {
					_charset->_left = _charset->_startLeft;
				}
				_charset->_top += fontHeight;
				break;
			default:
				break;
			}
		} else if ((c == 0xFF || (_game.version <= 6 && c == 0xFE)) && (_game.heversion <= 71)) {
			c = buf[i++];
			switch (c) {
			case 9:
			case 10:
			case 13:
			case 14:
				i += 2;
				break;
			case 1:
			case 8:
				if (_charset->_center) {
					_charset->_left = _charset->_startLeft - _charset->getStringWidth(a, buf + i);
				} else if (_game.version >= 4 && _game.version < 7 && _game.heversion == 0 && _language == Common::HE_ISR) {
					// Skip control characters as they might contain '\0' which results in incorrect string width.
					int ll = 0;
					byte *ltext = buf + i;
					while (ltext[ll] == 0xFF) {
						ll += 4;
					}
					byte lenbuf[270];
					memset(lenbuf, 0, sizeof(lenbuf));
					memcpy(lenbuf, ltext, ll);
					int u = ll;
					while (ltext[u]) {
						if ((ltext[u] == 0xFF || (_game.version <= 6 && ltext[u] == 0xFE)) && ltext[u + 1] == 8) {
							break;
						}
						u++;
					}
					memcpy(lenbuf, ltext, u);
					_charset->_left = _screenWidth - _charset->_startLeft - _charset->getStringWidth(a, lenbuf);
				} else {
					_charset->_left = _charset->_startLeft;
				}
				if (!(_game.platform == Common::kPlatformFMTowns) && _string[0].height) {
					_nextTop += _string[0].height;
				} else {
					_charset->_top += fontHeight;
				}
				break;
			case 12:
				color = buf[i] + (buf[i + 1] << 8);
				i += 2;
				if (color == 0xFF)
					_charset->setColor(_string[a].color);
				else
					_charset->setColor(color);
				break;
			default:
				break;
			}
		} else {
			if (a == 1 && _game.version >= 6) {
				// FIXME: The following code is a bit nasty. It is used for the
				// Highway surfing game in Sam&Max; there, _blitAlso is set to
				// true when writing the highscore numbers. It is also in DOTT
				// for parts the intro and for drawing newspaper headlines. It
				// is also used for scores in bowling mini game in fbear and
				// for names in load/save screen of all HE games. Maybe it is
				// also being used in other places.
				//
				// A better name for _blitAlso might be _imprintOnBackground

				if (_string[a].no_talk_anim == false) {
					//debug(0, "Would have set _charset->_blitAlso = true (wanted to print '%c' = %d)", c, c);
					_charset->_blitAlso = true;
				}
			}
			if (c & 0x80 && _useCJKMode) {
				if (checkSJISCode(c))
					c += buf[i++] * 256;
			}
			_charset->printChar(c, true);
			_charset->_blitAlso = false;
		}
	}

	if (a == 0) {
		_nextLeft = _charset->_left;
		_nextTop = _charset->_top;
	}

	_string[a].xpos = _charset->_str.right;

	if (_game.heversion >= 60) {
		_string[a]._default.xpos = _string[a].xpos;
		_string[a]._default.ypos = _string[a].ypos;
	}
}

int ScummEngine::convertMessageToString(const byte *msg, byte *dst, int dstSize) {
	uint num = 0;
	uint32 val;
	byte chr;
	byte lastChr = 0;
	const byte *src;
	byte *end;
	byte transBuf[384];

	assert(dst);
	end = dst + dstSize;

	if (msg == NULL) {
		debug(0, "Bad message in convertMessageToString, ignoring");
		return 0;
	}

	if (_game.version >= 7) {
		translateText(msg, transBuf);
		src = transBuf;
	} else {
		src = msg;
	}

	num = 0;

	while (1) {
		chr = src[num++];
		if (chr == 0)
			break;

		if (_game.id == GID_LOOM && _game.platform == Common::kPlatformPCEngine) {
			// Code for TM character
			if (chr == 0x0F && src[num] == 0x20) {
				*dst++ = 0x5D;
				*dst++ = 0x5E;
				continue;
			// Code for (C) character
			} else if (chr == 0x1C && src[num] == 0x20) {
				*dst++ = 0x3E;
				*dst++ = 0x2A;
				continue;
			// Code for " character
			} else if (chr == 0x19) {
				*dst++ = 0x2F;
				continue;
			}
		}

		if (chr == 0xFF) {
			chr = src[num++];

			// WORKAROUND for bug #985948, a script bug in Indy3. Apparently,
			// a german 'sz' was encoded incorrectly as 0xFF2E. We replace
			// this by the correct encoding here. See also ScummEngine::resStrLen().
			if (_game.id == GID_INDY3 && chr == 0x2E) {
				*dst++ = 0xE1;
				continue;
			}

			// WORKAROUND for bug #1514457: Yet another script bug in Indy3.
			// Once more a german 'sz' was encoded incorrectly, but this time
			// they simply encoded it as 0xFF instead of 0xE1. Happens twice
			// in script 71.
			if (_game.id == GID_INDY3 && chr == 0x20 && vm.slot[_currentScript].number == 71) {
				num--;
				*dst++ = 0xE1;
				continue;
			}

			if (chr == 1 || chr == 2 || chr == 3 || chr == 8) {
				// Simply copy these special codes
				*dst++ = 0xFF;
				*dst++ = chr;
			} else {
				val = (_game.version == 8) ? READ_LE_UINT32(src + num) : READ_LE_UINT16(src + num);
				switch (chr) {
				case 4:
					dst += convertIntMessage(dst, end - dst, val);
					break;
				case 5:
					dst += convertVerbMessage(dst, end - dst, val);
					break;
				case 6:
					dst += convertNameMessage(dst, end - dst, val);
					break;
				case 7:
					dst += convertStringMessage(dst, end - dst, val);
					break;
				case 9:
				case 10:
				case 12:
				case 13:
				case 14:
					// Simply copy these special codes
					*dst++ = 0xFF;
					*dst++ = chr;
					*dst++ = src[num+0];
					*dst++ = src[num+1];
					if (_game.version == 8) {
						*dst++ = src[num+2];
						*dst++ = src[num+3];
					}
					break;
				default:
					error("convertMessageToString(): string escape sequence %d unknown", chr);
				}
				num += (_game.version == 8) ? 4 : 2;
			}
		} else {
			if ((chr != '@') || (_game.id == GID_CMI && _language == Common::ZH_TWN) ||
				(_game.id == GID_LOOM && _game.platform == Common::kPlatformPCEngine && _language == Common::JA_JPN) ||
				(_game.platform == Common::kPlatformFMTowns && _language == Common::JA_JPN && checkSJISCode(lastChr))) {
				*dst++ = chr;
			}
			lastChr = chr;
		}

		// Check for a buffer overflow
		if (dst >= end)
			error("convertMessageToString: buffer overflow");
	}

	// WORKAROUND: Russian The Dig pads messages with 03. No idea why
	// it does not work as is with our rendering code, thus fixing it
	// with a workaround.
	if (_game.id == GID_DIG) {
		while (*(dst - 1) == 0x03)
			dst--;
	}
	*dst = 0;

	return dstSize - (end - dst);
}


int ScummEngine::convertIntMessage(byte *dst, int dstSize, int var) {
	int num;

	num = readVar(var);
	return snprintf((char *)dst, dstSize, "%d", num);
}

int ScummEngine::convertVerbMessage(byte *dst, int dstSize, int var) {
	int num, k;

	num = readVar(var);
	if (num) {
		for (k = 1; k < _numVerbs; k++) {
			// Fix ZAK FM-TOWNS bug #1013617 by emulating exact (inconsistant?) behavior of the original code
			if (num == _verbs[k].verbid && !_verbs[k].type && (!_verbs[k].saveid || (_game.version == 3 && _game.platform == Common::kPlatformFMTowns))) {
				const byte *ptr = getResourceAddress(rtVerb, k);
				return convertMessageToString(ptr, dst, dstSize);
			}
		}
	}
	return 0;
}

int ScummEngine::convertNameMessage(byte *dst, int dstSize, int var) {
	int num;

	num = readVar(var);
	if (num) {
		const byte *ptr = getObjOrActorName(num);
		if (ptr) {
			return convertMessageToString(ptr, dst, dstSize);
		}
	}
	return 0;
}

int ScummEngine::convertStringMessage(byte *dst, int dstSize, int var) {
	const byte *ptr;

	if (_game.version <= 2) {
		byte chr;
		int i = 0;
		while ((chr = (byte)_scummVars[var++])) {
			if (chr != '@') {
				*dst++ = chr;
				i++;
			}
		}

		return i;
	}

	if (_game.version == 3 || (_game.version >= 6 && _game.heversion < 72))
		var = readVar(var);

	if (var) {
		ptr = getStringAddress(var);
		if (ptr) {
			return convertMessageToString(ptr, dst, dstSize);
		}
	}
	return 0;
}


#pragma mark -
#pragma mark --- Charset initialisation ---
#pragma mark -



void ScummEngine::initCharset(int charsetno) {
	if (_game.id == GID_FT) {
		if (!_res->isResourceLoaded(rtCharset, charsetno))
			loadCharset(charsetno);
	} else {
		if (!getResourceAddress(rtCharset, charsetno))
			loadCharset(charsetno);
	}

	_string[0]._default.charset = charsetno;
	_string[1]._default.charset = charsetno;

	memcpy(_charsetColorMap, _charsetData[charsetno], sizeof(_charsetColorMap));
}


#pragma mark -
#pragma mark --- Translation/localization code ---
#pragma mark -



void ScummEngine::translateText(const byte *text, byte *trans_buff) {
	// Default: just copy the string
	memcpy(trans_buff, text, resStrLen(text) + 1);
}

} // End of namespace Scumm
