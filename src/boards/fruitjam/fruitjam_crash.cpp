// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See fruitjam_crash.h.
#if defined(ARDUINO_ADAFRUIT_FRUITJAM_RP2350)

#include "boards/fruitjam/fruitjam_crash.h"

#include <string.h>

#include "pico.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/m33_eppb.h"
#include "hardware/structs/timer.h"
#include "hardware/regs/intctrl.h"

// Watchdog scratch 0-3 are free (the SDK's reboot and enable magic use
// 4-7), survive a watchdog reset and are cleared by a real power-on. They
// carry the report, since RAM doesn't reliably survive the reboot: the
// first fault test found the RAM record zeroed afterwards (the boot path
// uses SRAM as workspace). So the essentials are packed in here:
//   0  FAULT: 0xFA in bits 24-31, the core in bit 21, CFSR's STKOF in bit
//      20, UFSR bits 16-19 in 16-19, MMFSR and BFSR in 0-15
//   0  STALL: the same with 0xFB in bits 24-31 (the NMI handler ran)
//   1  the frame in bits 8-31 and the phase in 0-7, kept live
//   2  the PC of a fault          3  its LR
#define SCRATCH_KIND  0
#define SCRATCH_PHASE 1
#define SCRATCH_PC    2
#define SCRATCH_LR    3
#define FAULT_MAGIC   0xFAu
#define STALL_MAGIC   0xFBu
#define STALL_IRQ     TIMER1_IRQ_0   // unused here; forced pending to raise the NMI
#define MAGIC_RECORD  0xC0FFEE42u   // g_rec below is initialised

// The full record, in RAM the runtime doesn't clear at boot, so it
// survives the reset. On power-up it holds garbage, which the magic
// rejects.
typedef struct {
    uint32_t magic;
    fruitjam_crash_t c;
} record_t;
static record_t __uninitialized_ram(g_rec);
static fruitjam_crash_t g_report;

void fruitjam_crash_begin(void) {
    const uint32_t k = watchdog_hw->scratch[SCRATCH_KIND];
    const bool stall = (k >> 24) == STALL_MAGIC;
    const bool fault = (k >> 24) == FAULT_MAGIC || stall;
    const bool hang = !fault && watchdog_enable_caused_reboot();
    if (g_rec.magic != MAGIC_RECORD) {
        memset(&g_rec, 0, sizeof g_rec);
        g_rec.magic = MAGIC_RECORD;
    }
    memset(&g_report, 0, sizeof g_report);
    if (fault) {
        // The RAM record, if it survived, has the rest (registers, SP,
        // fault addresses); the scratch registers are authoritative.
        if (g_rec.c.kind == FRUITJAM_CRASH_FAULT && g_rec.c.pc == watchdog_hw->scratch[SCRATCH_PC])
            g_report = g_rec.c;
        g_rec.c.count++;
        g_report.kind = stall ? FRUITJAM_CRASH_STALL : FRUITJAM_CRASH_FAULT;
        g_report.count = g_rec.c.count;
        g_report.core = (k >> 21) & 1u;
        g_report.cfsr = (k & 0xFFFFu) | (((k >> 16) & 0xFu) << 16) | (((k >> 20) & 1u) << 20);
        g_report.phase = watchdog_hw->scratch[SCRATCH_PHASE] & 0xFFu;
        g_report.frame = watchdog_hw->scratch[SCRATCH_PHASE] >> 8;
        g_report.pc = watchdog_hw->scratch[SCRATCH_PC];
        g_report.lr = watchdog_hw->scratch[SCRATCH_LR];
    } else if (hang) {
        g_rec.c.count++;
        g_report.kind = FRUITJAM_CRASH_HANG;
        g_report.phase = watchdog_hw->scratch[SCRATCH_PHASE] & 0xFFu;
        g_report.frame = watchdog_hw->scratch[SCRATCH_PHASE] >> 8;
        g_report.count = g_rec.c.count;
    }
    watchdog_hw->scratch[SCRATCH_KIND] = 0;
    watchdog_hw->scratch[SCRATCH_PHASE] = 0;
    watchdog_hw->scratch[SCRATCH_PC] = 0;
    watchdog_hw->scratch[SCRATCH_LR] = 0;
    g_rec.c.kind = FRUITJAM_CRASH_NONE;
    g_rec.c.frame = 0;
}

static volatile uint32_t g_frame = 0;       // core 0's last feed
static volatile uint32_t g_beat = 0;        // bumped by every feed
static volatile bool g_armed = false;
static uint32_t (*volatile g_probe)(void) = nullptr;

void fruitjam_crash_set_stall_probe(uint32_t (*probe)(void)) { g_probe = probe; }

void fruitjam_crash_start_watchdog(void) {
    // Core 0's own NMI mask: the stall IRQ becomes an NMI here only. It is
    // never enabled in either NVIC, so it does nothing else.
    eppb_hw->nmi_mask[0] |= 1u << STALL_IRQ;
    watchdog_enable(FRUITJAM_CRASH_WATCHDOG_MS, true);
    g_armed = true;
}

void fruitjam_crash_feed(uint32_t frame) {
    g_frame = frame;
    g_beat = g_beat + 1;
    watchdog_hw->scratch[SCRATCH_PHASE] = (frame << 8) | (watchdog_hw->scratch[SCRATCH_PHASE] & 0xFFu);
    watchdog_update();
}

void fruitjam_crash_phase(uint32_t phase) {
    watchdog_hw->scratch[SCRATCH_PHASE] = (g_frame << 8) | (phase & 0xFFu);
}

void __not_in_flash_func(fruitjam_crash_poll_core1)(void) {
    static uint32_t seen = 0, since = 0;
    static bool fired = false;
    if (!g_armed || fired) return;
    const uint32_t now = timer0_hw->timerawl;
    const uint32_t beat = g_beat;
    if (beat != seen || since == 0) { seen = beat; since = now; return; }
    if (now - since < FRUITJAM_CRASH_STALL_MS * 1000u) return;
    fired = true;
    timer1_hw->inte |= 1u;    // alarm 0's interrupt, forced on:
    timer1_hw->intf |= 1u;    // TIMER1_IRQ_0 -> core 0's NMI
}

const fruitjam_crash_t *fruitjam_crash_report(void) { return &g_report; }

// --- The fault handler -------------------------------------------------------
//
// isr_hardfault replaces the SDK's weak default (a breakpoint loop). The
// stub picks the stack the fault was taken on (EXC_RETURN bit 2: process
// or main; the SCUMM engine's coroutine runs on a stack of its own, in
// PSRAM) and passes the exception frame to the C half, which reads
// r0-r3, r12, LR, PC, xPSR from it.
extern "C" void fruitjam_crash_fault_c(const uint32_t *frame, uint32_t exc_return);

extern "C" void fruitjam_crash_fault_c(const uint32_t *frame, uint32_t exc_return) {
    (void)exc_return;
    fruitjam_crash_t *c = &g_rec.c;
    c->kind = FRUITJAM_CRASH_FAULT;
    c->core = get_core_num();
    c->r0 = frame[0]; c->r1 = frame[1]; c->r2 = frame[2]; c->r3 = frame[3];
    c->r12 = frame[4]; c->lr = frame[5]; c->pc = frame[6]; c->xpsr = frame[7];
    c->sp = (uint32_t)frame;
    c->cfsr  = *(volatile uint32_t *)0xE000ED28u;   // SCB->CFSR
    c->hfsr  = *(volatile uint32_t *)0xE000ED2Cu;   // SCB->HFSR
    c->mmfar = *(volatile uint32_t *)0xE000ED34u;   // SCB->MMFAR
    c->bfar  = *(volatile uint32_t *)0xE000ED38u;   // SCB->BFAR
    c->phase = watchdog_hw->scratch[SCRATCH_PHASE];
    g_rec.magic = MAGIC_RECORD;
    const uint32_t cfsr = c->cfsr;
    watchdog_hw->scratch[SCRATCH_PC] = c->pc;
    watchdog_hw->scratch[SCRATCH_LR] = c->lr;
    watchdog_hw->scratch[SCRATCH_KIND] = (FAULT_MAGIC << 24) | ((c->core & 1u) << 21) |
                                         (((cfsr >> 20) & 1u) << 20) |
                                         (((cfsr >> 16) & 0xFu) << 16) | (cfsr & 0xFFFFu);
    watchdog_reboot(0, 0, 1);
    for (;;) {}
}

// The NMI: core 1 saw core 0 stall. Records where core 0 was, as a fault
// does, but marked STALL.
extern "C" void fruitjam_crash_nmi_c(const uint32_t *frame, uint32_t exc_return);
extern "C" void fruitjam_crash_nmi_c(const uint32_t *frame, uint32_t exc_return) {
    (void)exc_return;
    timer1_hw->intf &= ~1u;
    const uint32_t cfsr = *(volatile uint32_t *)0xE000ED28u;
    watchdog_hw->scratch[SCRATCH_PC] = frame[6];
    watchdog_hw->scratch[SCRATCH_LR] = g_probe ? g_probe() : frame[5];
    watchdog_hw->scratch[SCRATCH_KIND] = (STALL_MAGIC << 24) | ((get_core_num() & 1u) << 21) |
                                         (((cfsr >> 20) & 1u) << 20) |
                                         (((cfsr >> 16) & 0xFu) << 16) | (cfsr & 0xFFFFu);
    watchdog_reboot(0, 0, 1);
    for (;;) {}
}

extern "C" void __attribute__((naked, used)) isr_nmi(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "b fruitjam_crash_nmi_c\n");
}

extern "C" void __attribute__((naked, used)) isr_hardfault(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "b fruitjam_crash_fault_c\n");
}

#endif // ARDUINO_ADAFRUIT_FRUITJAM_RP2350
