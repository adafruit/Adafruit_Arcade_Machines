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

// fruitjam-scumm: cut down from ScummVM's scummsys.h for two targets only,
// the macOS/Linux host harness and the Cortex-M natmod. Both are little
// endian and both read unaligned 16/32-bit words without trapping.

#ifndef COMMON_SCUMMSYS_H
#define COMMON_SCUMMSYS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <strings.h>

#if defined(__cplusplus)
#include <new>
#endif

#define SCUMM_LITTLE_ENDIAN
#define SCUMMVM_USE_PRAGMA_PACK

#define GCC_ATLEAST(major, minor) (defined(__GNUC__) && (__GNUC__ > (major) || (__GNUC__ == (major) && __GNUC_MINOR__ >= (minor))))

#define GCC_PRINTF(x,y) __attribute__((__format__(__printf__, x, y)))
#define MSVC_PRINTF
#define PACKED_STRUCT __attribute__((__packed__))
#define FORCEINLINE inline __attribute__((__always_inline__))
#define PLUGIN_EXPORT
#define NORETURN_PRE
#define NORETURN_POST __attribute__((__noreturn__))
#define WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))
#define STRINGBUFLEN 1024
#define MAXPATHLEN 256
#define scumm_va_copy va_copy

#define STATIC_ASSERT(expression, message) static_assert((expression), #message)

typedef unsigned char byte;
typedef unsigned char uint8;
typedef signed char int8;
typedef unsigned short uint16;
typedef signed short int16;
typedef unsigned int uint32;
typedef signed int int32;
typedef unsigned int uint;
typedef signed long long int64;
typedef unsigned long long uint64;
typedef uintptr_t uintptr;

#endif
