// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// The Fruit Jam's USB host: the two Type-A ports, driven by Pico PIO USB
// through Adafruit TinyUSB (extras/CONSOLES_PLAN.md, "USB gamepads on the
// Fruit Jam"). Needs Tools > USB Stack > Adafruit TinyUSB, and the sketch
// must #include <pio_usb.h> so the builder finds the Pico PIO USB library.
//
// The host runs on the core that calls fruitjam_usb_host_begin() -- core 0,
// since core 1 drives the display -- and its 1 ms frame timer interrupts
// that core (measured at ~1.2% of core 0; DEVNOTES). Call it AFTER the
// display is initialised, so video claims its PIO and DMA first, and after
// the 252 MHz clock change, which PIO-USB's timing is computed from.
#ifndef USB_HOST_FRUITJAM_H
#define USB_HOST_FRUITJAM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Powers the ports and starts the host on PIO 2 with a free DMA channel.
// Returns false if the clock is not a multiple of 12 MHz.
bool fruitjam_usb_host_begin(void);

// Runs the host stack: plug-ins, unplugs, and delivering reports. Call it
// often -- several times a frame -- since a report is only seen when this
// runs.
void fruitjam_usb_host_task(void);

// The DMA channel the host took (for the heartbeat), or -1 before begin().
int fruitjam_usb_host_dma_channel(void);

#ifdef __cplusplus
}
#endif

#endif
