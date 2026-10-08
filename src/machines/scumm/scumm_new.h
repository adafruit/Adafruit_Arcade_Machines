// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// C++ `new` and `delete` for the SCUMM sketch: once the engine's arena
// exists, allocations come from it; before that, and for anything not in
// it, the C library's heap. The same rule as upstream's host build
// (fruitjam-scumm libc/fj_cxx.cpp, FJ_HOST).
//
// INCLUDE THIS ONCE, in the SCUMM sketch's .ino, and nowhere else. It
// defines the global operators, so it can't be in the library: archived
// with dot_a_linkage, an object defining operator new would be pulled into
// EVERY sketch to satisfy `new`, and Pac-Man would allocate through the
// SCUMM allocator. machines/scumm/core/backend/fj_arduino.h has the same
// reasoning for malloc.
//
// The engine allocates almost everything with `new` (its objects, its
// containers), so without this its 0.4-1.1 MB would land in on-chip RAM,
// which doesn't have it. Anything else in the sketch that calls `new`
// after the engine starts lands in the arena too; that's harmless, as the
// arena is ordinary memory.
#ifndef SCUMM_NEW_H
#define SCUMM_NEW_H

#include <stddef.h>
#include <stdlib.h>
#include <new>

extern "C" {
void *fj_host_malloc(size_t n);
void fj_host_free(void *p);
int fj_alloc_ready(void);
int fj_alloc_owns(const void *p);
}

static void *scumm_new_(size_t n) {
    if (n == 0) n = 1;
    void *p = fj_alloc_ready() ? fj_host_malloc(n) : nullptr;
    return p ? p : malloc(n);
}

static void scumm_delete_(void *p) {
    if (!p) return;
    if (fj_alloc_owns(p)) fj_host_free(p);
    else free(p);
}

void *operator new(size_t n) { return scumm_new_(n); }
void *operator new[](size_t n) { return scumm_new_(n); }
void *operator new(size_t n, const std::nothrow_t &) noexcept { return scumm_new_(n); }
void *operator new[](size_t n, const std::nothrow_t &) noexcept { return scumm_new_(n); }
void operator delete(void *p) noexcept { scumm_delete_(p); }
void operator delete[](void *p) noexcept { scumm_delete_(p); }
void operator delete(void *p, size_t) noexcept { scumm_delete_(p); }
void operator delete[](void *p, size_t) noexcept { scumm_delete_(p); }

#endif
