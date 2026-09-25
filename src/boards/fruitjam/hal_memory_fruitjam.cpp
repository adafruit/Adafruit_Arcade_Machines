// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// ArcadeHAL memory contract on the Fruit Jam: bulk memory is its 8 MB PSRAM,
// through arduino-pico's PSRAM heap (pmalloc). See hal/arcade_hal_memory.h.
#if defined(ARDUINO_ADAFRUIT_FRUITJAM_RP2350)

#include <Arduino.h>
#include <psram.h>
#include "hal/arcade_hal_memory.h"
#include "board_config_fruitjam.h"

void *hal_mem_bulk_alloc(size_t size) { return pmalloc(size); }

size_t hal_mem_bulk_free(void) { return (size_t)rp2040.getFreePSRAMHeap(); }

// PSRAM TIMING MUST FOLLOW THE SYSTEM CLOCK. arduino-pico sets the PSRAM
// interface's clock divider and read delay at boot, for F_CPU (125 MHz by
// default on this board), and retimes it only for its own startup clock
// change (cores/rp2040/main.cpp). A sketch that then calls
// set_sys_clock_khz(252000) for PicoDVI leaves the divider chosen for
// 125 MHz, which at 252 MHz drives the PSRAM at 126 MHz, past its 109 MHz
// limit (RP2350_PSRAM_MAX_SCK_HZ), and the data comes back wrong: Link's
// Awakening, the first ROM loaded into PSRAM, failed its header checksum
// though the file itself was good.
//
// RETIME AFTER THE CLOCK CHANGE, NOT BEFORE. arduino-pico's own startup
// code retimes before raising the clock and passes the new frequency, but
// psram_reinit_timing(hz) ignores `hz`: psram_init() reads the CURRENT
// clk_sys. The first version followed that order, so it computed timing for
// 125 MHz, changed nothing, and a PSRAM self-test then read 72% of bytes
// wrong, with two reads of the same memory disagreeing. Changing the clock
// first leaves the PSRAM briefly overclocked, which is harmless because
// nothing can touch it in between (next paragraph); then the dummy access
// and barrier the datasheet asks for.
//
// NOTHING MAY RUN FROM FLASH WHILE THE PSRAM IS RETIMED: psram_init() puts
// the shared QMI interface into direct mode, which takes flash off the bus.
// arduino-pico does it at boot, before anything else runs. From setup(),
// core 1 is already spinning in setup1() -- from flash -- and USB interrupts
// are live. An earlier attempt did it with both running: the board went
// silent, core 1 never started the display, and core 0 blocked forever
// waiting for a scanline buffer. So the other core is parked and interrupts
// are off for the duration, as arduino-pico does around flash writes.
void fruitjam_set_sys_clock_khz(uint32_t khz) {
    rp2040.idleOtherCore();
    noInterrupts();
    set_sys_clock_khz(khz, true);
    psram_reinit_timing(); // reads the clock just set
    extern uint8_t __psram_start__;
    volatile uint8_t *x = &__psram_start__;
    *x ^= 0xff;
    *x ^= 0xff;
    asm volatile("" ::: "memory");
    interrupts();
    rp2040.resumeOtherCore();
}

#endif // ARDUINO_ADAFRUIT_FRUITJAM_RP2350
