#include "fj_arduino.h" // Adafruit Arcade Machines build mode
/* fruitjam-scumm - SCUMM v3/v4 on the Adafruit Fruit Jam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arena allocator: boundary-tagged blocks, free blocks in size-class bins
 * (first fit inside a bin), neighbours merged on free. 8-byte aligned.
 *
 * Block layout: [size | used | prev used] payload... and, for free blocks only, the
 * size repeated in the last word so the next block can find its start.
 * Free blocks keep next/prev bin links in their payload.
 */

#include "fj_alloc.h"

#include <stdint.h>
#include <string.h>

#if defined(FJ_HOST)
/* The host harness keeps the system allocator for itself and gives the
 * engine these (fj_host_arena.h renames the engine's calls). */
#define malloc fj_host_malloc
#define free fj_host_free
#define calloc fj_host_calloc
#define realloc fj_host_realloc
#endif

/* packed so the 64-bit host test can place it at 4 mod 8 like ARM does */
typedef struct __attribute__((packed, aligned(4))) FreeBlock {
	uint32_t head;
	struct FreeBlock *next, *prev;
} FreeBlock;

#define USED 1u
#define PREV_USED 2u
#define FLAGS 3u
#define NBINS 12
#define MIN_BLOCK ((uint32_t)((sizeof(FreeBlock) + 4 + 7) & ~7u)) /* 16 on ARM */

static uint8_t *heap_lo, *heap_hi;
static FreeBlock *bins[NBINS];
static size_t used_bytes, peak_bytes;

static inline uint32_t bsize(const void *b) {
	return *(const uint32_t *)b & ~FLAGS;
}

static int bin_of(uint32_t size) {
	int b = 0;
	size >>= 5; /* 32 bytes and less -> bin 0 */
	while (size && b < NBINS - 1) {
		size >>= 1;
		b++;
	}
	return b;
}

static void unlink_free(FreeBlock *f) {
	if (f->prev)
		f->prev->next = f->next;
	else
		bins[bin_of(bsize(f))] = f->next;
	if (f->next)
		f->next->prev = f->prev;
}

static void insert_free(uint8_t *p, uint32_t size, uint32_t prev_used) {
	FreeBlock *f = (FreeBlock *)p;
	f->head = size | prev_used;
	*(uint32_t *)(p + size - 4) = size;
	int b = bin_of(size);
	f->prev = 0;
	f->next = bins[b];
	if (f->next)
		f->next->prev = f;
	bins[b] = f;
	/* the block after us now has a free predecessor */
	if (p + size < heap_hi)
		*(uint32_t *)(p + size) &= ~PREV_USED;
}

void fj_alloc_init(void *arena, size_t size) {
	/* Blocks start 4 bytes past an 8-byte boundary so that payloads, after
	 * the one-word header, are 8-byte aligned. */
	uintptr_t lo = (((uintptr_t)arena + 7) & ~(uintptr_t)7) + 4;
	uintptr_t hi = ((uintptr_t)arena + size) & ~(uintptr_t)7;
	memset(bins, 0, sizeof(bins));
	used_bytes = peak_bytes = 0;
	heap_lo = (uint8_t *)lo;
	/* keep the last word as a permanently used sentinel */
	heap_hi = (uint8_t *)hi - 4;
	*(uint32_t *)heap_hi = 8 | USED;
	insert_free(heap_lo, (uint32_t)(heap_hi - heap_lo), PREV_USED);
	*(uint32_t *)heap_hi = 8 | USED; /* insert_free cleared PREV_USED */
}

size_t fj_alloc_used(void) {
	return used_bytes;
}

size_t fj_alloc_peak(void) {
	return peak_bytes;
}

void *malloc(size_t n) {
	if (!heap_lo)
		return 0;
	uint32_t need = (uint32_t)((n + 4 + 7) & ~(size_t)7);
	if (need < MIN_BLOCK)
		need = MIN_BLOCK;
	for (int b = bin_of(need); b < NBINS; b++) {
		for (FreeBlock *f = bins[b]; f; f = f->next) {
			uint32_t size = bsize(f);
			if (size < need)
				continue;
			unlink_free(f);
			uint8_t *p = (uint8_t *)f;
			uint32_t prev_used = f->head & PREV_USED;
			if (size - need >= MIN_BLOCK) {
				insert_free(p + need, size - need, PREV_USED);
				size = need;
			} else {
				*(uint32_t *)(p + size) |= PREV_USED;
			}
			*(uint32_t *)p = size | USED | prev_used;
			used_bytes += size;
			if (used_bytes > peak_bytes)
				peak_bytes = used_bytes;
			return p + 4;
		}
	}
	return 0;
}

void free(void *ptr) {
	if (!ptr)
		return;
	uint8_t *p = (uint8_t *)ptr - 4;
	uint32_t size = bsize(p);
	uint32_t prev_used = *(uint32_t *)p & PREV_USED;
	used_bytes -= size;
	/* merge with the next block */
	uint8_t *next = p + size;
	if (next < heap_hi && !(*(uint32_t *)next & USED)) {
		unlink_free((FreeBlock *)next);
		size += bsize(next);
	}
	/* merge with the previous block */
	if (!prev_used) {
		uint32_t psize = *(uint32_t *)(p - 4);
		uint8_t *prev = p - psize;
		unlink_free((FreeBlock *)prev);
		prev_used = *(uint32_t *)prev & PREV_USED;
		p = prev;
		size += psize;
	}
	insert_free(p, size, prev_used);
}

void *calloc(size_t a, size_t b) {
	size_t n = a * b;
	void *p = malloc(n);
	if (p)
		memset(p, 0, n);
	return p;
}

void *realloc(void *ptr, size_t n) {
	if (!ptr)
		return malloc(n);
	if (!n) {
		free(ptr);
		return 0;
	}
	uint32_t have = bsize((uint8_t *)ptr - 4) - 4;
	if (have >= n)
		return ptr;
	void *q = malloc(n);
	if (q) {
		memcpy(q, ptr, have);
		free(ptr);
	}
	return q;
}

int fj_alloc_ready(void) {
	return heap_lo != 0;
}

int fj_alloc_owns(const void *p) {
	return (const uint8_t *)p >= heap_lo && (const uint8_t *)p < heap_hi;
}
