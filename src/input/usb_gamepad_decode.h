// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// USB gamepad reports -> a standard set of buttons (extras/CONSOLES_PLAN.md,
// "USB gamepads on the Fruit Jam"). Pure decoding: no USB stack, so the
// host test (extras/tools/usb_gamepad_test) runs it against reports
// captured from real controllers.
//
// THREE KINDS OF CONTROLLER, all seen in the first captures (DEVNOTES):
//
//   GENERIC, with a table. A HID report descriptor says where the buttons,
//   hat switch and X/Y axes are, so the parser finds them. It does NOT say
//   which button is which: the same "button 1" was X on the Mantapad and Y
//   on the Retro-bit Genesis pad. So each known controller has a table from
//   HID button number to standard button, and unknown ones get the common
//   PlayStation-style numbering. The D-pad may be the X/Y axes, the hat
//   switch, or both (the Genesis pad declares a hat and never moves it), so
//   both are read and combined.
//
//   FIXED LAYOUT. Some descriptors are too big for the USB stack to deliver
//   at all (the DualShock 4's ~500 bytes, against Adafruit TinyUSB's
//   256-byte enumeration buffer), so those controllers are decoded from a
//   known report layout, by vendor/product ID.
//
//   UNSUPPORTED. No usable descriptor and not in the table.
//
// STANDARD BUTTONS are by POSITION, as SDL names them: SOUTH is the bottom
// face button, EAST the right one, and so on. What each position does in a
// game is the sketch's mapping. With Nintendo's layout, A is EAST and B is
// SOUTH; on an Xbox pad the button labelled A is SOUTH.
#ifndef USB_GAMEPAD_DECODE_H
#define USB_GAMEPAD_DECODE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    USB_PAD_UP     = 1u << 0,
    USB_PAD_DOWN   = 1u << 1,
    USB_PAD_LEFT   = 1u << 2,
    USB_PAD_RIGHT  = 1u << 3,
    USB_PAD_SOUTH  = 1u << 4,  // bottom face button
    USB_PAD_EAST   = 1u << 5,  // right
    USB_PAD_WEST   = 1u << 6,  // left
    USB_PAD_NORTH  = 1u << 7,  // top
    USB_PAD_L1     = 1u << 8,
    USB_PAD_R1     = 1u << 9,
    USB_PAD_L2     = 1u << 10,
    USB_PAD_R2     = 1u << 11,
    USB_PAD_SELECT = 1u << 12, // Select, Share, Back, Mode
    USB_PAD_START  = 1u << 13, // Start, Options
    USB_PAD_HOME   = 1u << 14, // Home, PS, Guide
    USB_PAD_L3     = 1u << 15,
    USB_PAD_R3     = 1u << 16,
    USB_PAD_TOUCH  = 1u << 17, // DualShock 4 touchpad click
};
#define USB_PAD_BUTTON_COUNT 18

#define USB_PAD_MAX_HID_BUTTONS 32

typedef enum {
    USB_PAD_UNSUPPORTED = 0,
    USB_PAD_GENERIC,        // descriptor-parsed, with a button table
    USB_PAD_DUALSHOCK4,     // fixed layout
} usb_pad_kind_t;

typedef struct {
    uint16_t bit;           // offset in the report, in bits, report ID included
    uint8_t  size;          // bits
    int32_t  min, max;      // logical range
} usb_pad_field_t;

typedef struct {
    usb_pad_kind_t kind;
    const char    *name;    // the table entry's name, or "generic"
    uint8_t  report_id;     // 0: the device sends no report IDs

    // GENERIC: where things are, from the descriptor.
    uint8_t  n_buttons;
    uint16_t button_bit[USB_PAD_MAX_HID_BUTTONS]; // HID button i+1
    bool     has_hat, has_x, has_y;
    usb_pad_field_t hat, x, y;

    // HID button i+1 -> USB_PAD_* (0: unused).
    uint32_t button_map[USB_PAD_MAX_HID_BUTTONS];
} usb_pad_layout_t;

// Works out how to read a controller, from its IDs and its HID report
// descriptor (which may be NULL/0 when the stack could not deliver it).
// Returns the kind; USB_PAD_UNSUPPORTED means its reports can't be read.
usb_pad_kind_t usb_pad_layout_init(usb_pad_layout_t *layout, uint16_t vid, uint16_t pid,
                                   const uint8_t *desc, uint16_t desc_len);

// Decodes one report into USB_PAD_* bits. Returns false for a report that
// is not the controller's input report (another report ID, too short, or
// zero-length, as some pads send on unplug), leaving *buttons untouched.
bool usb_pad_decode(const usb_pad_layout_t *layout, const uint8_t *report, uint16_t len,
                    uint32_t *buttons);

const char *usb_pad_button_name(unsigned bit_index);

#ifdef __cplusplus
}
#endif

#endif
