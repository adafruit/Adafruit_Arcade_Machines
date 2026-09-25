// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See wii_classic.h.
#include "wii_classic.h"

#include <Arduino.h>
#include <Wire.h>

namespace {

constexpr uint8_t  kAddr = 0x52;
constexpr uint32_t kRetryMs = 1000;  // between attempts to find a controller

uint8_t g_code; // the latest Wire result, for the diagnostics

bool write_reg(TwoWire *w, uint8_t reg, uint8_t val) {
    w->beginTransmission(kAddr);
    w->write(reg);
    w->write(val);
    g_code = w->endTransmission();
    return g_code == 0;
}

bool write_ptr(TwoWire *w, uint8_t reg) {
    w->beginTransmission(kAddr);
    w->write(reg);
    g_code = w->endTransmission();
    return g_code == 0;
}

bool read_n(TwoWire *w, uint8_t *buf, uint8_t n) {
    g_code = (uint8_t)w->requestFrom(kAddr, n);
    if (g_code != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = (uint8_t)w->read();
    return true;
}

void note_fail(wii_classic_t *p, uint8_t step) {
    if (step < 5) p->step_fails[step]++;
    p->last_fail_step = step;
    p->last_fail_code = g_code;
}

void go_absent(wii_classic_t *p, uint32_t now_ms) {
    if (p->state == WII_STATE_READY) p->drops++;
    p->state = WII_STATE_ABSENT;
    p->buttons = 0;
    p->requested = false;
    p->step_at_ms = now_ms + kRetryMs;
}

// Report bytes b4/b5 (standard) or b6/b7 (high resolution), active low.
uint16_t decode(uint8_t hi, uint8_t lo) {
    const uint8_t h = (uint8_t)~hi, l = (uint8_t)~lo;
    uint16_t v = 0;
    if (l & 0x01) v |= WII_BTN_UP;
    if (h & 0x40) v |= WII_BTN_DOWN;
    if (l & 0x02) v |= WII_BTN_LEFT;
    if (h & 0x80) v |= WII_BTN_RIGHT;
    if (l & 0x10) v |= WII_BTN_A;
    if (l & 0x40) v |= WII_BTN_B;
    if (l & 0x08) v |= WII_BTN_X;
    if (l & 0x20) v |= WII_BTN_Y;
    if (h & 0x20) v |= WII_BTN_L;
    if (h & 0x02) v |= WII_BTN_R;
    if (l & 0x80) v |= WII_BTN_ZL;
    if (l & 0x04) v |= WII_BTN_ZR;
    if (h & 0x04) v |= WII_BTN_PLUS;
    if (h & 0x10) v |= WII_BTN_MINUS;
    if (h & 0x08) v |= WII_BTN_HOME;
    return v;
}

} // namespace

void wii_classic_begin(wii_classic_t *p, TwoWire *wire) {
    *p = wii_classic_t{};
    p->wire = wire;
    p->state = WII_STATE_ABSENT;
    p->step_at_ms = 0; // try on the first service()
}

// Start-up steps, each one I2C transaction, spaced by the delays other
// implementations found necessary (NintendoExtensionCtrl: 10 ms after 0xF0,
// 20 ms after 0xFB).
void wii_classic_service(wii_classic_t *p, uint32_t now_ms) {
    if (p->state == WII_STATE_READY) return;
    if ((int32_t)(now_ms - p->step_at_ms) < 0) return;

    TwoWire *w = p->wire;
    if (p->state == WII_STATE_ABSENT) {
        if (!write_reg(w, 0xF0, 0x55)) { note_fail(p, 0); go_absent(p, now_ms); return; }
        p->state = WII_STATE_STARTING;
        p->step = 1;
        p->step_at_ms = now_ms + 10;
        return;
    }
    switch (p->step) {
    case 1:
        if (!write_reg(w, 0xFB, 0x00)) { note_fail(p, 1); go_absent(p, now_ms); return; }
        p->step = 2; p->step_at_ms = now_ms + 20;
        return;
    case 2:
        // Ask for high resolution. A knockoff may refuse or ignore it; the
        // format is decided per read either way.
        if (!write_reg(w, 0xFE, 0x03)) note_fail(p, 2); // noted, not fatal
        p->step = 3; p->step_at_ms = now_ms + 2;
        return;
    case 3:
        if (!write_ptr(w, 0xFA)) { note_fail(p, 3); go_absent(p, now_ms); return; }
        p->step = 4; p->step_at_ms = now_ms + 1;
        return;
    case 4:
        if (!read_n(w, p->id, 6)) { note_fail(p, 4); go_absent(p, now_ms); return; }
        if (p->id[2] != 0xA4 || p->id[3] != 0x20) {
            g_code = 0xA4;
            note_fail(p, 4);
            go_absent(p, now_ms);
            return;
        }
        p->state = WII_STATE_READY;
        p->connects++;
        p->requested = false;
        return;
    }
}

void wii_classic_request(wii_classic_t *p) {
    if (p->state != WII_STATE_READY) return;
    if (!write_ptr(p->wire, 0x00)) { go_absent(p, millis()); return; }
    p->requested = true;
}

void wii_classic_collect(wii_classic_t *p) {
    if (p->state != WII_STATE_READY || !p->requested) return;
    p->requested = false;
    if (!read_n(p->wire, p->raw, 8)) { go_absent(p, millis()); return; }
    p->hires = !(p->raw[6] == 0x00 && p->raw[7] == 0x00);
    p->buttons = p->hires ? decode(p->raw[6], p->raw[7]) : decode(p->raw[4], p->raw[5]);
}

const char *wii_classic_button_name(unsigned i) {
    static const char *const kNames[WII_BTN_COUNT] = {
        "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y",
        "L", "R", "ZL", "ZR", "START", "SELECT", "HOME",
    };
    return i < WII_BTN_COUNT ? kNames[i] : "?";
}
