// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// USB gamepads as Fruit Jam buttons (extras/CONSOLES_PLAN.md, "USB gamepads
// on the Fruit Jam", step 4). A USB pad's buttons are merged into
// hal_input_read(): a HAL_BTN_* reads as pressed if its GPIO button OR any
// connected USB pad has it held. The machines don't change.
//
// Which pad button is which HAL_BTN_* depends on the kind of game and, for
// some pads, on the pad (the Retro-bit Genesis pad has its own tables), so
// a sketch picks a mapping when it starts the host. The tables are in
// usb_input_fruitjam.cpp and recorded in CONSOLES_PLAN.md.
//
// Needs Tools > USB Stack > Adafruit TinyUSB, and the sketch must
// #include <pio_usb.h>. Without USE_TINYUSB none of this is compiled and
// hal_input_read() is GPIO only, as before.
#ifndef USB_INPUT_FRUITJAM_H
#define USB_INPUT_FRUITJAM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FRUITJAM_USB_MAP_ARCADE = 0,  // the arcade games
    FRUITJAM_USB_MAP_GAMEBOY,     // the Game Boy console
    // The NES reads the same four buttons the same way (A, B, Start,
    // Select, on SHOOT, ACTION2, START1, COIN), so it shares the tables.
    FRUITJAM_USB_MAP_NES = FRUITJAM_USB_MAP_GAMEBOY,
} fruitjam_usb_map_t;

// Starts the USB host (usb_host_fruitjam.h) and picks the mapping. Call
// AFTER the display is initialised (it claims PIO 0 first) and after the
// 252 MHz clock change. The host task then also runs whenever core 0 waits
// for a free scanline buffer (hal_video_fruitjam.cpp), several times a
// frame, in time the game has to spare.
bool fruitjam_usb_input_begin(fruitjam_usb_map_t map);

// Once per frame, before the sketch reads its buttons: runs the host task
// once more and recomputes which HAL_BTN_* the connected pads hold.
void fruitjam_usb_input_poll(void);

// Whether any connected USB pad holds this HAL_BTN_* (as of the last poll).
// hal_input_read() ORs this in.
bool fruitjam_usb_input_held(uint8_t hal_btn);

// Connected pads, for the heartbeat.
uint8_t fruitjam_usb_input_pads(void);

#ifdef __cplusplus
}
#endif

#endif
