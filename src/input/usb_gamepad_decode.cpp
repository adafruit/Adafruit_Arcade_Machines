// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See usb_gamepad_decode.h. The descriptor parser follows the USB HID 1.11
// specification's item format (section 6.2.2); only what a gamepad needs is
// kept: input fields for buttons (usage page 9), the hat switch (0x01/0x39)
// and the X/Y axes (0x01/0x30, 0x31).
#include "usb_gamepad_decode.h"

#include <string.h>

namespace {

// --- known controllers ------------------------------------------------------
// Each table lists USB_PAD_* for HID buttons 1, 2, 3, ... as captured on
// hardware (DEVNOTES). Positions follow the controller's own layout.

// The common PlayStation-style DirectInput numbering, for unknown pads:
// 1 left, 2 bottom, 3 right, 4 top, then shoulders, Select/Start, sticks.
const uint32_t kDefaultMap[] = {
    USB_PAD_WEST, USB_PAD_SOUTH, USB_PAD_EAST, USB_PAD_NORTH,
    USB_PAD_L1, USB_PAD_R1, USB_PAD_L2, USB_PAD_R2,
    USB_PAD_SELECT, USB_PAD_START, USB_PAD_L3, USB_PAD_R3, USB_PAD_HOME,
};

// Mantapad SNES-style (081F:E401): X=1 A=2 B=3 Y=4 L=5 R=6 Select=9 Start=10.
// SNES positions: X top, A right, B bottom, Y left. (Select and Start were
// first recorded the other way round; playing Ms. Pac-Man showed Start
// inserting coins, and corrected it.)
const uint32_t kMantapadMap[] = {
    USB_PAD_NORTH, USB_PAD_EAST, USB_PAD_SOUTH, USB_PAD_WEST,
    USB_PAD_L1, USB_PAD_R1, 0, 0,
    USB_PAD_SELECT, USB_PAD_START,
};

// Retro-bit 8-button Genesis pad (0F0D:00C1): Y=1 B=2 A=3 X=4 Z=7 C=8
// Mode=9 Start=10. Bottom row A B C -> west, south, east; top row: Y ->
// north, and X and Z, which have no face position left, -> L1 and R1.
const uint32_t kRetrobitGenesisMap[] = {
    USB_PAD_NORTH, USB_PAD_SOUTH, USB_PAD_WEST, USB_PAD_L1,
    0, 0, USB_PAD_R1, USB_PAD_EAST,
    USB_PAD_SELECT, USB_PAD_START,
};

struct known_t {
    uint16_t vid, pid;
    usb_pad_kind_t kind;
    const char *name;
    const uint32_t *map;
    uint8_t map_len;
};

#define MAP(m) m, (uint8_t)(sizeof m / sizeof m[0])
const known_t kKnown[] = {
    { 0x081F, 0xE401, USB_PAD_GENERIC,    "Mantapad (SNES-style)",   MAP(kMantapadMap) },
    { 0x0F0D, 0x00C1, USB_PAD_GENERIC,    "Retro-bit Genesis 8-button", MAP(kRetrobitGenesisMap) },
    { 0x054C, 0x05C4, USB_PAD_DUALSHOCK4, "DualShock 4",             nullptr, 0 },
    { 0x054C, 0x09CC, USB_PAD_DUALSHOCK4, "DualShock 4 (v2)",        nullptr, 0 },
};

// --- descriptor parsing ------------------------------------------------------

int32_t item_signed(uint32_t v, uint8_t size) {
    if (size == 1) return (int8_t)v;
    if (size == 2) return (int16_t)v;
    return (int32_t)v;
}

struct parse_t {
    usb_pad_layout_t *L;
    // Global items
    uint16_t usage_page = 0;
    int32_t  lmin = 0, lmax = 0;
    uint32_t lmax_raw = 0; uint8_t lmax_size = 0;
    uint8_t  report_size = 0, report_count = 0, report_id = 0;
    // Local items
    uint32_t usages[16]; uint8_t n_usages = 0;
    uint32_t umin = 0, umax = 0; bool have_range = false;
    // Where the input report we are laying out has got to, in bits.
    bool     id_chosen = false;
    uint16_t bit = 0;

    void clear_local() { n_usages = 0; have_range = false; umin = umax = 0; }

    // The usage of field element i: a list, a range, or the last listed.
    uint32_t usage_of(uint8_t i) const {
        if (have_range) return (umin + i <= umax) ? umin + i : umax;
        if (n_usages) return usages[i < n_usages ? i : n_usages - 1];
        return 0;
    }

    int32_t logical_max() const {
        // A 1-byte 0xFF means 255 to most devices, though the spec reads it
        // as -1; when the minimum is not negative, read the maximum unsigned.
        if (lmin >= 0 && lmax < 0) return (int32_t)lmax_raw;
        return lmax;
    }

    void input(uint32_t flags) {
        const uint16_t total = (uint16_t)(report_size * report_count);
        // Lay out one input report only: the first one with an ID, or the
        // only one if there are no IDs. Fields of other reports are skipped.
        if (!id_chosen) { id_chosen = true; L->report_id = report_id; bit = report_id ? 8 : 0; }
        if (report_id != L->report_id) { clear_local(); return; }
        const bool constant = flags & 0x01, variable = flags & 0x02;
        if (!constant && variable) {
            for (uint8_t i = 0; i < report_count; i++) {
                const uint32_t u = usage_of(i);
                const uint16_t page = (u >> 16) ? (uint16_t)(u >> 16) : usage_page;
                const uint16_t id = (uint16_t)u;
                const uint16_t at = (uint16_t)(bit + i * report_size);
                usb_pad_field_t f = { at, report_size, lmin, logical_max() };
                if (page == 0x09 && id >= 1 && id <= USB_PAD_MAX_HID_BUTTONS && report_size == 1) {
                    L->button_bit[id - 1] = at;
                    if (id > L->n_buttons) L->n_buttons = (uint8_t)id;
                } else if (page == 0x01 && id == 0x39 && !L->has_hat) {
                    L->hat = f; L->has_hat = true;
                } else if (page == 0x01 && id == 0x30 && !L->has_x) {
                    L->x = f; L->has_x = true;
                } else if (page == 0x01 && id == 0x31 && !L->has_y) {
                    L->y = f; L->has_y = true;
                }
            }
        }
        bit = (uint16_t)(bit + total);
        clear_local();
    }

    void run(const uint8_t *d, uint16_t n) {
        uint16_t i = 0;
        while (i < n) {
            const uint8_t prefix = d[i++];
            if (prefix == 0xFE) { // long item: skip it
                if (i + 1 >= n) return;
                i = (uint16_t)(i + 2 + d[i]);
                continue;
            }
            const uint8_t sz = (prefix & 3) == 3 ? 4 : (prefix & 3);
            if (i + sz > n) return;
            uint32_t v = 0;
            for (uint8_t k = 0; k < sz; k++) v |= (uint32_t)d[i + k] << (8 * k);
            i = (uint16_t)(i + sz);
            const uint8_t type = (prefix >> 2) & 3, tag = prefix >> 4;
            if (type == 0) {        // Main
                if (tag == 0x8) input(v);             // Input
                else if (tag == 0x9 || tag == 0xB) clear_local(); // Output, Feature
                else clear_local();                    // Collection, End Collection
            } else if (type == 1) { // Global
                switch (tag) {
                case 0x0: usage_page = (uint16_t)v; break;
                case 0x1: lmin = item_signed(v, sz); break;
                case 0x2: lmax = item_signed(v, sz); lmax_raw = v; lmax_size = sz; break;
                case 0x7: report_size = (uint8_t)v; break;
                case 0x8: report_id = (uint8_t)v; break;
                case 0x9: report_count = (uint8_t)v; break;
                default: break; // physical, unit, push/pop: not needed
                }
            } else if (type == 2) { // Local
                const uint32_t u = (sz == 4) ? v : ((uint32_t)usage_page << 16) | (v & 0xFFFF);
                switch (tag) {
                case 0x0: if (n_usages < 16) usages[n_usages++] = u; break;
                case 0x1: umin = u; have_range = true; break;
                case 0x2: umax = u; have_range = true; break;
                default: break;
                }
            }
        }
    }
};

// A little-endian bit field of up to 32 bits, or 0 past the end.
uint32_t field(const uint8_t *r, uint16_t len, uint16_t bit, uint8_t size) {
    uint32_t v = 0;
    for (uint8_t k = 0; k < size; k++) {
        const uint16_t b = (uint16_t)(bit + k);
        if ((b >> 3) >= len) break;
        if (r[b >> 3] & (1u << (b & 7))) v |= 1u << k;
    }
    return v;
}

int32_t field_signed(const uint8_t *r, uint16_t len, const usb_pad_field_t &f) {
    uint32_t v = field(r, len, f.bit, f.size);
    if (f.min < 0 && f.size < 32 && (v & (1u << (f.size - 1)))) v |= ~0u << f.size;
    return (int32_t)v;
}

// An axis beyond a quarter of its range from centre is a D-pad direction.
uint32_t axis_dirs(int32_t v, int32_t min, int32_t max, uint32_t neg, uint32_t pos) {
    const int32_t quarter = (max - min) / 4;
    if (v <= min + quarter) return neg;
    if (v >= max - quarter) return pos;
    return 0;
}

// Hat switch: 0 up, clockwise in eighths; anything else is released.
uint32_t hat_dirs(int32_t v, int32_t min, int32_t max) {
    static const uint32_t k8[8] = {
        USB_PAD_UP, USB_PAD_UP | USB_PAD_RIGHT, USB_PAD_RIGHT, USB_PAD_DOWN | USB_PAD_RIGHT,
        USB_PAD_DOWN, USB_PAD_DOWN | USB_PAD_LEFT, USB_PAD_LEFT, USB_PAD_UP | USB_PAD_LEFT,
    };
    const int32_t d = v - min;
    if (max - min == 7 && d >= 0 && d < 8) return k8[d];
    if (max - min == 3 && d >= 0 && d < 4) return k8[d * 2];
    return 0;
}

} // namespace

usb_pad_kind_t usb_pad_layout_init(usb_pad_layout_t *L, uint16_t vid, uint16_t pid,
                                   const uint8_t *desc, uint16_t desc_len) {
    memset(L, 0, sizeof *L);
    L->name = "generic";
    const known_t *known = nullptr;
    for (const known_t &k : kKnown) if (k.vid == vid && k.pid == pid) known = &k;

    if (known && known->kind == USB_PAD_DUALSHOCK4) {
        L->kind = USB_PAD_DUALSHOCK4;
        L->name = known->name;
        L->report_id = 0x01;
        return L->kind;
    }

    if (!desc || !desc_len) return L->kind = USB_PAD_UNSUPPORTED;
    parse_t p; p.L = L;
    p.run(desc, desc_len);
    if (L->n_buttons == 0 && !L->has_hat && !(L->has_x && L->has_y))
        return L->kind = USB_PAD_UNSUPPORTED;

    const uint32_t *map = kDefaultMap;
    uint8_t map_len = (uint8_t)(sizeof kDefaultMap / sizeof kDefaultMap[0]);
    if (known) { map = known->map; map_len = known->map_len; L->name = known->name; }
    for (uint8_t i = 0; i < map_len && i < USB_PAD_MAX_HID_BUTTONS; i++) L->button_map[i] = map[i];
    return L->kind = USB_PAD_GENERIC;
}

bool usb_pad_decode(const usb_pad_layout_t *L, const uint8_t *r, uint16_t len, uint32_t *out) {
    if (!r || len == 0) return false;
    if (L->report_id && r[0] != L->report_id) return false;

    uint32_t b = 0;
    if (L->kind == USB_PAD_DUALSHOCK4) {
        // Report 0x01 over USB: 1-4 sticks LX LY RX RY, 5 hat (low nibble)
        // and Square/Cross/Circle/Triangle (high), 6 L1 R1 L2 R2 Share
        // Options L3 R3, 7 PS and touchpad (bits 0-1), 8-9 L2/R2 analog.
        if (len < 8) return false;
        b |= hat_dirs(r[5] & 0x0F, 0, 7);
        b |= axis_dirs(r[1], 0, 255, USB_PAD_LEFT, USB_PAD_RIGHT);
        b |= axis_dirs(r[2], 0, 255, USB_PAD_UP, USB_PAD_DOWN);
        if (r[5] & 0x10) b |= USB_PAD_WEST;   // Square
        if (r[5] & 0x20) b |= USB_PAD_SOUTH;  // Cross
        if (r[5] & 0x40) b |= USB_PAD_EAST;   // Circle
        if (r[5] & 0x80) b |= USB_PAD_NORTH;  // Triangle
        static const uint32_t k6[8] = {
            USB_PAD_L1, USB_PAD_R1, USB_PAD_L2, USB_PAD_R2,
            USB_PAD_SELECT, USB_PAD_START, USB_PAD_L3, USB_PAD_R3,
        };
        for (int i = 0; i < 8; i++) if (r[6] & (1u << i)) b |= k6[i];
        if (r[7] & 0x01) b |= USB_PAD_HOME;
        if (r[7] & 0x02) b |= USB_PAD_TOUCH;
        *out = b;
        return true;
    }
    if (L->kind != USB_PAD_GENERIC) return false;

    for (uint8_t i = 0; i < L->n_buttons; i++)
        if (L->button_map[i] && field(r, len, L->button_bit[i], 1)) b |= L->button_map[i];
    if (L->has_hat) b |= hat_dirs(field_signed(r, len, L->hat), L->hat.min, L->hat.max);
    if (L->has_x) b |= axis_dirs(field_signed(r, len, L->x), L->x.min, L->x.max, USB_PAD_LEFT, USB_PAD_RIGHT);
    if (L->has_y) b |= axis_dirs(field_signed(r, len, L->y), L->y.min, L->y.max, USB_PAD_UP, USB_PAD_DOWN);
    *out = b;
    return true;
}

const char *usb_pad_button_name(unsigned i) {
    static const char *const kNames[USB_PAD_BUTTON_COUNT] = {
        "UP", "DOWN", "LEFT", "RIGHT", "SOUTH", "EAST", "WEST", "NORTH",
        "L1", "R1", "L2", "R2", "SELECT", "START", "HOME", "L3", "R3", "TOUCH",
    };
    return i < USB_PAD_BUTTON_COUNT ? kNames[i] : "?";
}
