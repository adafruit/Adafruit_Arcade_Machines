// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// USB gamepads through the Adafruit TinyUSB host (extras/CONSOLES_PLAN.md,
// "USB gamepads on the Fruit Jam"). This file keeps track of the connected
// controllers and decodes their reports with usb_gamepad_decode; starting
// the host itself is the board's job (boards/fruitjam/usb_host_fruitjam.h).
//
// Compiled only when the sketch selects Tools > USB Stack > Adafruit
// TinyUSB (USE_TINYUSB); otherwise the functions below don't exist, and a
// sketch that calls them fails to link, which is the intent.
//
// Players are numbered in the order controllers were plugged in; a
// controller keeps its number until it is unplugged.
#ifndef USB_GAMEPAD_H
#define USB_GAMEPAD_H

#include <stdint.h>
#include <stdbool.h>

#include "input/usb_gamepad_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_GAMEPAD_MAX 4

// USB_PAD_* held on player `player` (0 = the first), or 0 if none there.
uint32_t usb_gamepad_buttons(uint8_t player);

typedef struct {
    bool     connected;
    uint16_t vid, pid;
    const char *name;        // the decoder's name for it
    usb_pad_kind_t kind;
    uint32_t reports;        // reports decoded since it was plugged in
} usb_gamepad_info_t;
bool usb_gamepad_info(uint8_t player, usb_gamepad_info_t *out);

// HID devices seen that could not be read as a gamepad (no usable report
// descriptor and not in the table), since boot.
uint32_t usb_gamepad_unsupported_count(void);

// Optional hooks for a self-test, which prints what controllers send. Called
// from the host task: on every HID interface plugged in, with its descriptor
// and whether it was taken as a gamepad (`player`, or -1); and on every
// report from a gamepad, raw, with what it decoded to.
typedef void (*usb_gamepad_mount_hook_t)(uint8_t dev_addr, uint8_t instance,
                                         uint16_t vid, uint16_t pid,
                                         const uint8_t *desc, uint16_t desc_len,
                                         const usb_pad_layout_t *layout, int player);
typedef void (*usb_gamepad_report_hook_t)(int player, const uint8_t *report, uint16_t len,
                                          uint32_t buttons);
void usb_gamepad_set_hooks(usb_gamepad_mount_hook_t on_mount,
                           usb_gamepad_report_hook_t on_report);

#ifdef __cplusplus
}
#endif

#endif
