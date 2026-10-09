/* fruitjam-scumm - SCUMM v3/v4 on the Adafruit Fruit Jam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * One coroutine: the SCUMM engine runs its own main loop on a private stack
 * and yields back to the host once per frame (and for file I/O). The host
 * and the engine never run at the same time.
 */

#ifndef FJ_CORO_H
#define FJ_CORO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*fj_coro_fn)(void);

/* Set up the coroutine to run fn on stack[0..size). Does not start it. */
void fj_coro_create(void *stack, size_t size, fj_coro_fn fn);

/* Host: run the coroutine until it yields. */
void fj_coro_resume(void);

/* Engine: give control back to the host. Returns on the next resume. */
void fj_coro_yield(void);

/* Deepest the coroutine's stack has been, in bytes. */
size_t fj_coro_stack_used(void);

/* Nonzero while executing on the coroutine's stack. */
int fj_coro_active(void);

#ifdef __cplusplus
}
#endif

#endif
