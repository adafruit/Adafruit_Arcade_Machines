// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Crash reports for the Fruit Jam (RP2350), for bring-up. Without this a
// fault parks the core in the SDK's default handler: the picture freezes
// (core 1 keeps scanning out), the sound stops, USB stops answering, and
// nothing says where.
//
//   - A FAULT (HardFault, and the faults that escalate to it) saves the
//     faulting PC, LR, xPSR, the stacked registers and the fault status
//     registers, then reboots.
//   - A HANG, a core stuck with interrupts off or locked up so that no
//     handler runs, is caught by the hardware watchdog: once started, it
//     reboots the board if fruitjam_crash_feed() isn't called for
//     FRUITJAM_CRASH_WATCHDOG_MS.
//   - A STALL, core 0 not feeding for FRUITJAM_CRASH_STALL_MS while core 1
//     still runs: core 1 (fruitjam_crash_poll_core1()) raises a
//     non-maskable interrupt on core 0, whose handler records the PC it
//     interrupted, even with interrupts off, then reboots. A core in
//     LOCKUP (a fault inside a fault) can't take even an NMI; then the
//     watchdog fires and the report is a HANG with no PC.
//   - fruitjam_crash_phase() keeps a running note of what core 0 is doing,
//     in a watchdog scratch register, which survives the reset; so even a
//     hang says where it happened.
//
// After the reboot, fruitjam_crash_report() returns the last crash, for
// the sketch to print (repeatedly: the boot messages are lost on this
// board). Map a PC to a function with
//   arm-none-eabi-addr2line -f -C -e <build>/<sketch>.ino.elf 0x<pc>
#ifndef FRUITJAM_CRASH_H
#define FRUITJAM_CRASH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRUITJAM_CRASH_WATCHDOG_MS 5000u
#define FRUITJAM_CRASH_STALL_MS    2000u

typedef enum {
    FRUITJAM_CRASH_NONE = 0,
    FRUITJAM_CRASH_FAULT,      // a fault handler ran: the registers are valid
    FRUITJAM_CRASH_HANG,       // the watchdog fired: only the phase is known
    FRUITJAM_CRASH_STALL,      // core 1's NMI caught core 0 stuck: PC valid, LR = the probe word
} fruitjam_crash_kind_t;

typedef struct {
    fruitjam_crash_kind_t kind;
    uint32_t count;            // crashes since power-up
    uint32_t core;             // the core that faulted
    uint32_t pc, lr, xpsr, sp;
    uint32_t r0, r1, r2, r3, r12;
    uint32_t cfsr, hfsr, mmfar, bfar;
    uint32_t phase;            // the last fruitjam_crash_phase() value
    uint32_t frame;            // the last fruitjam_crash_feed() frame number
} fruitjam_crash_t;

// At boot: reads what the last reset left. Call before anything else that
// might reset the scratch registers (early in setup()).
void fruitjam_crash_begin(void);

// Starts the watchdog and the stall check (after the slow boot steps, on
// core 0), then feed once a frame.
void fruitjam_crash_start_watchdog(void);
void fruitjam_crash_feed(uint32_t frame);

// On core 1, often (e.g. once a video frame): fires the NMI on a stall. In
// RAM, safe to call from the video interrupt.
void fruitjam_crash_poll_core1(void);

// A STALL report keeps the PC and, instead of the LR, one word from this
// probe, called inside the NMI: whatever the sketch needs to see at the
// moment of the stall (e.g. a driver's DMA state). Must be in RAM.
void fruitjam_crash_set_stall_probe(uint32_t (*probe)(void));

// What core 0 is doing now: any number 0-255 the sketch chooses.
void fruitjam_crash_phase(uint32_t phase);

// The crash that caused the last reset; kind NONE if it wasn't one.
const fruitjam_crash_t *fruitjam_crash_report(void);

#ifdef __cplusplus
}
#endif

#endif
