/* fruitjam-scumm - SCUMM v3/v4 on the Adafruit Fruit Jam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Heap for the natmod build: malloc/free/new/delete all come out of one
 * arena (a bytearray the Python adapter keeps alive), so nothing the engine
 * allocates is ever seen, moved or collected by MicroPython's GC. The host
 * harness uses it too, through fj_host_arena.h.
 */

#ifndef FJ_ALLOC_H
#define FJ_ALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void fj_alloc_init(void *arena, size_t size);
size_t fj_alloc_used(void);
size_t fj_alloc_peak(void);
int fj_alloc_owns(const void *p); /* p is inside the arena */
int fj_alloc_ready(void);           /* fj_alloc_init has run */

#ifdef __cplusplus
}
#endif

#endif
