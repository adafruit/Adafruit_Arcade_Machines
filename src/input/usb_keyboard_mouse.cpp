// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See usb_keyboard_mouse.h.
#include "input/usb_keyboard_mouse.h"

#if defined(USE_TINYUSB)

#include <string.h>
#include <Adafruit_TinyUSB.h>

namespace {

struct hid_dev_t {
    bool    used;
    uint8_t dev_addr, instance;
};

hid_dev_t g_kbd, g_mouse;

// Keyboard: the keys held in the last report, to see which are new.
uint8_t g_held[6];

#define QUEUE 16u
usb_key_event_t g_queue[QUEUE];
uint32_t g_head, g_tail;   // pushed, popped (same core: no locking)

// Mouse.
int32_t g_dx, g_dy, g_wheel;
uint8_t g_buttons;

bool is(const hid_dev_t &d, uint8_t dev_addr, uint8_t instance) {
    return d.used && d.dev_addr == dev_addr && d.instance == instance;
}

void keyboard_report(const uint8_t *r, uint16_t len) {
    if (len < 8) return;
    // 0x01 in every key slot is "too many keys" (phantom state): ignore it.
    if (r[2] == 0x01) return;
    for (int i = 2; i < 8; i++) {
        const uint8_t u = r[i];
        if (u < 0x04) continue;
        bool was_held = false;
        for (uint8_t h : g_held) if (h == u) { was_held = true; break; }
        if (was_held) continue;
        if (g_head - g_tail >= QUEUE) g_tail++;   // full: drop the oldest
        g_queue[g_head % QUEUE] = usb_key_event_t{ u, r[0] };
        g_head++;
    }
    memcpy(g_held, r + 2, 6);
}

void mouse_report(const uint8_t *r, uint16_t len) {
    if (len < 3) return;
    g_buttons = r[0] & 0x07u;
    g_dx += (int8_t)r[1];
    g_dy += (int8_t)r[2];
    if (len >= 4) g_wheel += (int8_t)r[3];
}

} // namespace

extern "C" {

bool usb_keyboard_mouse_mount(uint8_t dev_addr, uint8_t instance, uint8_t protocol) {
    hid_dev_t *d = protocol == HID_ITF_PROTOCOL_KEYBOARD ? &g_kbd
             : protocol == HID_ITF_PROTOCOL_MOUSE    ? &g_mouse : nullptr;
    if (!d) return false;
    d->used = true;
    d->dev_addr = dev_addr;
    d->instance = instance;
    if (d == &g_kbd) memset(g_held, 0, sizeof g_held);
    else { g_dx = g_dy = g_wheel = 0; g_buttons = 0; }
    tuh_hid_receive_report(dev_addr, instance);
    return true;
}

bool usb_keyboard_mouse_report(uint8_t dev_addr, uint8_t instance,
                               const uint8_t *report, uint16_t len) {
    if (is(g_kbd, dev_addr, instance)) keyboard_report(report, len);
    else if (is(g_mouse, dev_addr, instance)) mouse_report(report, len);
    else return false;
    return true;
}

void usb_keyboard_mouse_umount(uint8_t dev_addr, uint8_t instance) {
    if (is(g_kbd, dev_addr, instance)) { g_kbd.used = false; memset(g_held, 0, sizeof g_held); }
    if (is(g_mouse, dev_addr, instance)) { g_mouse.used = false; g_buttons = 0; }
}

bool usb_keyboard_pop(usb_key_event_t *out) {
    if (g_tail == g_head) return false;
    *out = g_queue[g_tail % QUEUE];
    g_tail++;
    return true;
}

uint8_t usb_mouse_take(int32_t *dx, int32_t *dy, int32_t *wheel) {
    if (dx) *dx = g_dx;
    if (dy) *dy = g_dy;
    if (wheel) *wheel = g_wheel;
    g_dx = g_dy = g_wheel = 0;
    return g_buttons;
}

bool usb_keyboard_connected(void) { return g_kbd.used; }
bool usb_mouse_connected(void) { return g_mouse.used; }

} // extern "C"

#endif // USE_TINYUSB
