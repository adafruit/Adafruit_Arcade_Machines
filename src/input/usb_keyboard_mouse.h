// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// USB keyboards and mice through the Adafruit TinyUSB host, in their boot
// protocols (which TinyUSB's host selects by default): a keyboard's 8-byte
// report (modifiers, a reserved byte, up to six keys held) and a mouse's
// buttons, X, Y and wheel. Board-agnostic, like usb_gamepad.h, whose
// TinyUSB callbacks hand these interfaces over (TinyUSB allows only one set
// of HID callbacks); starting the host is the board's job.
//
// Key presses are queued as HID usages with the modifiers held at the
// time; what a usage means is the consumer's business. Mouse motion is
// summed until taken. Everything is filled and read on the core that runs
// the USB host task, so no locking.
//
// Compiled only with Tools > USB Stack > Adafruit TinyUSB (USE_TINYUSB).
#ifndef USB_KEYBOARD_MOUSE_H
#define USB_KEYBOARD_MOUSE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// HID modifier bits (the keyboard report's first byte).
#define USB_KBD_MOD_LCTRL  0x01u
#define USB_KBD_MOD_LSHIFT 0x02u
#define USB_KBD_MOD_LALT   0x04u
#define USB_KBD_MOD_LGUI   0x08u
#define USB_KBD_MOD_RCTRL  0x10u
#define USB_KBD_MOD_RSHIFT 0x20u
#define USB_KBD_MOD_RALT   0x40u
#define USB_KBD_MOD_RGUI   0x80u
#define USB_KBD_MOD_SHIFT  (USB_KBD_MOD_LSHIFT | USB_KBD_MOD_RSHIFT)

typedef struct {
    uint8_t usage;      // HID keyboard usage (0x04 'a' ... 0x52 Up)
    uint8_t modifiers;  // USB_KBD_MOD_* held when it was pressed
} usb_key_event_t;

// The next key pressed since the last call, oldest first; false if none.
// Holds 16; older presses are dropped beyond that.
bool usb_keyboard_pop(usb_key_event_t *out);

// Mouse buttons held (bit 0 left, 1 right, 2 middle) and the motion since
// the last call, which this resets. Y is down-positive, as the mouse sends
// it; wheel is up-positive.
uint8_t usb_mouse_take(int32_t *dx, int32_t *dy, int32_t *wheel);

bool usb_keyboard_connected(void);
bool usb_mouse_connected(void);

// For usb_gamepad.cpp's TinyUSB callbacks. mount takes the interface if
// it's a boot keyboard or mouse (true) and starts its reports.
bool usb_keyboard_mouse_mount(uint8_t dev_addr, uint8_t instance, uint8_t protocol);
bool usb_keyboard_mouse_report(uint8_t dev_addr, uint8_t instance,
                               const uint8_t *report, uint16_t len);
void usb_keyboard_mouse_umount(uint8_t dev_addr, uint8_t instance);

#ifdef __cplusplus
}
#endif

#endif
