/* fruitjam-scumm - SCUMM v3/v4 on the Adafruit Fruit Jam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Force-included into every engine and backend file of the host harness
 * build (Makefile.host) so the engine allocates from the arena exactly as
 * it does on the board. new/delete are in src/libc/fj_cxx.cpp.
 */

#ifndef FJ_HOST_ARENA_H
#define FJ_HOST_ARENA_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
void *fj_host_malloc(size_t n);
void fj_host_free(void *p);
void *fj_host_calloc(size_t a, size_t b);
void *fj_host_realloc(void *p, size_t n);
#ifdef __cplusplus
}
#include <cstdlib>
#endif

#define malloc fj_host_malloc
#define free fj_host_free
#define calloc fj_host_calloc
#define realloc fj_host_realloc

#endif
