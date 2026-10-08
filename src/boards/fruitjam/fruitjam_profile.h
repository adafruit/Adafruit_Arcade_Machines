// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// A sampling profiler for core 0 on the Fruit Jam (RP2350), for bring-up:
// a timer interrupt at FRUITJAM_PROFILE_HZ, at a high priority (above all
// but the USB host's frame timer), records the PC it interrupted into a
// histogram of 64-byte buckets. The
// sketch prints the busiest buckets now and then; map each to a function
// with
//   arm-none-eabi-addr2line -f -C -e <build>/<sketch>.ino.elf 0x<bucket>
// Samples can be limited to a window (fruitjam_profile_enable()), e.g. the
// part of a frame worth looking at. Uses a free TIMER0 alarm and its IRQ.
#ifndef FRUITJAM_PROFILE_H
#define FRUITJAM_PROFILE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRUITJAM_PROFILE_HZ 5000u

// On core 0: allocates the histogram (~34 KB) and starts sampling.
bool fruitjam_profile_begin(void);

// Count samples only while enabled (default: enabled).
void fruitjam_profile_enable(bool on);

typedef struct { uint32_t addr; uint32_t count; } fruitjam_profile_bucket_t;

// The `n` busiest buckets since the last call, busiest first, and the
// total samples taken (counted, outside the buckets, or disabled). Resets
// the histogram.
uint32_t fruitjam_profile_take(fruitjam_profile_bucket_t *out, uint32_t n,
                               uint32_t *total, uint32_t *outside);

#ifdef __cplusplus
}
#endif

#endif
