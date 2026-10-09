#include "fj_arduino.h" // Adafruit Arcade Machines build mode
/* fruitjam-scumm - SCUMM v3/v4 on the Adafruit Fruit Jam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Coroutine switch. On Cortex-M (Thumb-2, hard float) it is a hand-written
 * stack swap saving the AAPCS callee-saved registers r4-r11, lr and
 * s16-s31. On a computer the coroutine is a pthread handed a baton, which
 * gives the same strictly alternating behaviour.
 */

#include "fj_coro.h"

#include <stdint.h>

static fj_coro_fn coro_fn;
static int coro_on;
static unsigned char *stack_lo;
static size_t stack_size;

#define STACK_PAINT 0xA5

/* Fill the stack with a pattern so fj_coro_stack_used can find the high
 * water mark. */
static void paint(void *stack, size_t size) {
	stack_lo = stack;
	stack_size = size;
	for (size_t i = 0; i < size; i++)
		stack_lo[i] = STACK_PAINT;
}

size_t fj_coro_stack_used(void) {
	size_t i = 0;
	while (i < stack_size && stack_lo[i] == STACK_PAINT)
		i++;
	return stack_size - i;
}

int fj_coro_active(void) {
	return coro_on;
}

#if defined(__arm__)

static void *host_sp;
static void *coro_sp;

/* Armv8-M (the RP2350's Cortex-M33) has a stack limit register, and the
 * boot ROM or SDK may have set it to the bottom of the main stack. The
 * coroutine stack is in the arena (PSRAM), below that, so the limit is
 * lifted while the coroutine runs and put back afterwards. The module is
 * built for Armv7-M, which has no MSPLIM/PSPLIM, so the instructions are
 * emitted as raw encodings and only used on an Armv8-M Mainline core. */
static int has_splim;

static int core_has_splim(void) {
	uint32_t cpuid = *(volatile uint32_t *)0xE000ED00;
	uint32_t partno = (cpuid >> 4) & 0xFFF;
	return partno == 0xD21 || partno == 0xD22 || partno == 0xD23 || partno == 0xD31;
}

static int on_psp(void) {
	uint32_t control;
	__asm__ volatile ("mrs %0, control" : "=r" (control));
	return (control & 2) != 0;
}

static uint32_t splim_get(int psp) {
	uint32_t v;
	if (psp)
		__asm__ volatile (".inst.w 0xF3EF800B\n\tmov %0, r0" : "=r" (v) : : "r0"); /* mrs r0, psplim */
	else
		__asm__ volatile (".inst.w 0xF3EF800A\n\tmov %0, r0" : "=r" (v) : : "r0"); /* mrs r0, msplim */
	return v;
}

static void splim_set(int psp, uint32_t v) {
	if (psp)
		__asm__ volatile ("mov r0, %0\n\t.inst.w 0xF380880B" : : "r" (v) : "r0", "memory"); /* msr psplim, r0 */
	else
		__asm__ volatile ("mov r0, %0\n\t.inst.w 0xF380880A" : : "r" (v) : "r0", "memory"); /* msr msplim, r0 */
}

/* void fj_coro_switch(void **save, void *load): push callee-saved state,
 * store sp in *save, load sp, pop and return into the other context. */
void fj_coro_switch(void **save, void *load);
__asm__(
	"	.text\n"
	"	.thumb\n"
	"	.syntax unified\n"
	"	.global fj_coro_switch\n"
	"	.type fj_coro_switch, %function\n"
	"	.thumb_func\n"
	"fj_coro_switch:\n"
	"	push {r4-r11, lr}\n"
#if defined(__ARM_FP)
	"	vpush {s16-s31}\n"
#endif
	"	str sp, [r0]\n"
	"	mov sp, r1\n"
#if defined(__ARM_FP)
	"	vpop {s16-s31}\n"
#endif
	"	pop {r4-r11, pc}\n"
	"	.size fj_coro_switch, . - fj_coro_switch\n"
);

static void coro_entry(void) {
	coro_fn();
	/* The engine never returns, but if it did: park here for good. */
	for (;;)
		fj_coro_yield();
}

void fj_coro_create(void *stack, size_t size, fj_coro_fn fn) {
	coro_fn = fn;
	paint(stack, size);
	uintptr_t top = ((uintptr_t)stack + size) & ~(uintptr_t)7;
	/* Frame popped by the first switch: [s16-s31] r4-r11 lr. */
#if defined(__ARM_FP)
	const int words = 16 + 8 + 1;
#else
	const int words = 8 + 1;
#endif
	uint32_t *sp = (uint32_t *)(top - words * 4);
	for (int i = 0; i < words - 1; i++)
		sp[i] = 0;
	sp[words - 1] = (uint32_t)(uintptr_t)coro_entry; /* Thumb bit is set */
	coro_sp = sp;
	has_splim = core_has_splim();
}

void fj_coro_resume(void) {
	int psp = 0;
	uint32_t limit = 0;
	if (has_splim) {
		psp = on_psp();
		limit = splim_get(psp);
		splim_set(psp, 0);
	}
	coro_on = 1;
	fj_coro_switch(&host_sp, coro_sp);
	coro_on = 0;
	if (has_splim)
		splim_set(psp, limit);
}

void fj_coro_yield(void) {
	fj_coro_switch(&coro_sp, host_sp);
}

#else /* host: pthreads */

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

static pthread_t coro_thread;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int turn; /* 0 = host runs, 1 = coroutine runs */
static int started;
static void *coro_stack;
static size_t coro_stack_size;

static void wait_turn(int me) {
	while (turn != me)
		pthread_cond_wait(&cond, &lock);
}

static void *coro_main(void *arg) {
	(void)arg;
	pthread_mutex_lock(&lock);
	wait_turn(1);
	pthread_mutex_unlock(&lock);
	coro_fn();
	for (;;)
		fj_coro_yield();
	return 0;
}

void fj_coro_create(void *stack, size_t size, fj_coro_fn fn) {
	/* pthreads wants a page-aligned stack: use a copy of the same size so
	 * the host run shows whether the board's stack is big enough (host
	 * frames are bigger than Thumb ones). */
	(void)stack;
	long page = sysconf(_SC_PAGESIZE);
	size = (size + page - 1) / page * page;
	void *p = 0;
	if (posix_memalign(&p, page, size))
		abort();
	coro_fn = fn;
	coro_stack = p;
	coro_stack_size = size;
	paint(p, size);
}

void fj_coro_resume(void) {
	if (!started) {
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setstack(&attr, coro_stack, coro_stack_size);
		pthread_create(&coro_thread, &attr, coro_main, 0);
		started = 1;
	}
	pthread_mutex_lock(&lock);
	coro_on = 1;
	turn = 1;
	pthread_cond_broadcast(&cond);
	wait_turn(0);
	coro_on = 0;
	pthread_mutex_unlock(&lock);
}

void fj_coro_yield(void) {
	pthread_mutex_lock(&lock);
	turn = 0;
	pthread_cond_broadcast(&cond);
	wait_turn(1);
	pthread_mutex_unlock(&lock);
}

#endif
