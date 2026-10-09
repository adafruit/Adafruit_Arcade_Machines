// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See fruitjam_profile.h.
#if defined(ARDUINO_ADAFRUIT_FRUITJAM_RP2350)

#include "boards/fruitjam/fruitjam_profile.h"

#include <stdlib.h>
#include <string.h>

#include "pico.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/structs/timer.h"

#define FLASH_LO 0x10000000u
#define FLASH_SPAN (1u << 20)          // 1 MB of code
#define RAM_LO 0x20000000u
#define RAM_SPAN 0x82000u              // all of SRAM, scratch X/Y included
#define SHIFT 6                         // 64-byte buckets
#define NFLASH (FLASH_SPAN >> SHIFT)
#define NRAM (RAM_SPAN >> SHIFT)

static uint16_t *g_flash = nullptr, *g_ram = nullptr;
static volatile uint32_t g_total = 0, g_outside = 0;
static volatile bool g_on = true;
static int g_alarm = -1;
static uint32_t g_period_us = 1000000u / FRUITJAM_PROFILE_HZ;

static inline void bump(uint16_t *b) { if (*b != 0xFFFFu) (*b)++; }

extern "C" void __not_in_flash_func(fruitjam_profile_sample_c)(const uint32_t *frame) {
    timer0_hw->intr = 1u << g_alarm;
    timer0_hw->alarm[g_alarm] = timer0_hw->timerawl + g_period_us;
    g_total = g_total + 1;
    if (!g_on) return;
    const uint32_t pc = frame[6];
    if (pc - FLASH_LO < FLASH_SPAN) bump(&g_flash[(pc - FLASH_LO) >> SHIFT]);
    else if (pc - RAM_LO < RAM_SPAN) bump(&g_ram[(pc - RAM_LO) >> SHIFT]);
    else g_outside = g_outside + 1;
}

// The vector points straight here (irq_set_exclusive_handler), so the
// interrupted code's exception frame is on MSP or PSP, by EXC_RETURN.
extern "C" void __attribute__((naked)) __not_in_flash_func(fruitjam_profile_isr)(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "push {r4, lr}\n"
        "ldr r1, =fruitjam_profile_sample_c\n"
        "blx r1\n"
        "pop {r4, pc}\n"
        ".ltorg\n");
}

bool fruitjam_profile_begin(void) {
    if (g_alarm >= 0) return true;
    g_flash = (uint16_t *)calloc(NFLASH, sizeof(uint16_t));
    g_ram = (uint16_t *)calloc(NRAM, sizeof(uint16_t));
    if (!g_flash || !g_ram) return false;
    g_alarm = hardware_alarm_claim_unused(false);
    if (g_alarm < 0) return false;
    const uint irq = hardware_alarm_get_irq_num(g_alarm);
    irq_set_exclusive_handler(irq, fruitjam_profile_isr);
    // Just below the top: Pico PIO USB's frame timer must not be
    // interrupted mid-packet (usb_host_fruitjam.cpp), so it can't be
    // sampled; everything else can.
    irq_set_priority(irq, 0x40);
    timer0_hw->inte |= 1u << g_alarm;
    irq_set_enabled(irq, true);
    timer0_hw->alarm[g_alarm] = timer0_hw->timerawl + g_period_us;
    return true;
}

void fruitjam_profile_enable(bool on) { g_on = on; }

uint32_t fruitjam_profile_take(fruitjam_profile_bucket_t *out, uint32_t n,
                               uint32_t *total, uint32_t *outside) {
    if (total) *total = g_total;
    if (outside) *outside = g_outside;
    g_total = 0;
    g_outside = 0;
    if (!g_flash) return 0;
    uint32_t got = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t best = 0, at = 0;
        bool in_ram = false;
        for (uint32_t i = 0; i < NFLASH; i++)
            if (g_flash[i] > best) { best = g_flash[i]; at = i; in_ram = false; }
        for (uint32_t i = 0; i < NRAM; i++)
            if (g_ram[i] > best) { best = g_ram[i]; at = i; in_ram = true; }
        if (!best) break;
        out[got].addr = (in_ram ? RAM_LO : FLASH_LO) + (at << SHIFT);
        out[got].count = best;
        got++;
        (in_ram ? g_ram : g_flash)[at] = 0;   // taken
    }
    memset(g_flash, 0, NFLASH * sizeof(uint16_t));
    memset(g_ram, 0, NRAM * sizeof(uint16_t));
    return got;
}

#endif // ARDUINO_ADAFRUIT_FRUITJAM_RP2350
