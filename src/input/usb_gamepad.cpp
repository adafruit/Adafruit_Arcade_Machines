// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See usb_gamepad.h.
#include "input/usb_gamepad.h"

#if defined(USE_TINYUSB)

#include <string.h>
#include <Adafruit_TinyUSB.h>

namespace {

struct pad_t {
    bool     used;
    uint8_t  dev_addr, instance;
    uint16_t vid, pid;
    usb_pad_layout_t layout;
    volatile uint32_t buttons;
    uint32_t reports;
};

pad_t    g_pads[USB_GAMEPAD_MAX];
uint32_t g_unsupported;
usb_gamepad_mount_hook_t  g_on_mount;
usb_gamepad_report_hook_t g_on_report;

pad_t *find(uint8_t dev_addr, uint8_t instance) {
    for (pad_t &p : g_pads)
        if (p.used && p.dev_addr == dev_addr && p.instance == instance) return &p;
    return nullptr;
}

int player_of(const pad_t *slot) {
    int n = 0;
    for (const pad_t &p : g_pads) {
        if (&p == slot) return n;
        if (p.used) n++;
    }
    return -1;
}

} // namespace

extern "C" {

// TinyUSB calls these from tuh_task(), on the core that runs the host.

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, const uint8_t *desc, uint16_t desc_len) {
    // Keyboards and mice have their own boot protocols; not gamepads.
    const uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
    if (proto == HID_ITF_PROTOCOL_KEYBOARD || proto == HID_ITF_PROTOCOL_MOUSE) return;

    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    usb_pad_layout_t layout;
    const bool usable = usb_pad_layout_init(&layout, vid, pid, desc, desc_len) != USB_PAD_UNSUPPORTED;
    pad_t *slot = nullptr;
    if (usable) for (pad_t &p : g_pads) if (!p.used) { slot = &p; break; }
    if (!usable) g_unsupported++;
    if (!slot) {
        if (g_on_mount) g_on_mount(dev_addr, instance, vid, pid, desc, desc_len, &layout, -1);
        return;
    }
    slot->layout = layout;
    slot->dev_addr = dev_addr;
    slot->instance = instance;
    slot->vid = vid;
    slot->pid = pid;
    slot->buttons = 0;
    slot->reports = 0;
    slot->used = true;
    if (g_on_mount) g_on_mount(dev_addr, instance, vid, pid, desc, desc_len, &layout, player_of(slot));
    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (pad_t *p = find(dev_addr, instance)) { p->buttons = 0; p->used = false; }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, const uint8_t *report, uint16_t len) {
    if (pad_t *p = find(dev_addr, instance)) {
        uint32_t b = p->buttons;
        if (usb_pad_decode(&p->layout, report, len, &b)) { p->buttons = b; p->reports++; }
        if (g_on_report) g_on_report(player_of(p), report, len, p->buttons);
    }
    tuh_hid_receive_report(dev_addr, instance);
}

} // extern "C"

// Player n is the n-th used slot, so numbers close up when one is unplugged
// only for the players after it.
static pad_t *player_slot(uint8_t player) {
    uint8_t n = 0;
    for (pad_t &p : g_pads) if (p.used && n++ == player) return &p;
    return nullptr;
}

uint32_t usb_gamepad_buttons(uint8_t player) {
    pad_t *p = player_slot(player);
    return p ? p->buttons : 0;
}

bool usb_gamepad_info(uint8_t player, usb_gamepad_info_t *out) {
    memset(out, 0, sizeof *out);
    pad_t *p = player_slot(player);
    if (!p) return false;
    out->connected = true;
    out->vid = p->vid;
    out->pid = p->pid;
    out->name = p->layout.name;
    out->kind = p->layout.kind;
    out->reports = p->reports;
    return true;
}

uint32_t usb_gamepad_unsupported_count(void) { return g_unsupported; }

void usb_gamepad_set_hooks(usb_gamepad_mount_hook_t on_mount, usb_gamepad_report_hook_t on_report) {
    g_on_mount = on_mount;
    g_on_report = on_report;
}

#endif // USE_TINYUSB
