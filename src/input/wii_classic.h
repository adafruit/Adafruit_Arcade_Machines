// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Wii Classic-protocol controllers over I2C: the Wii Classic Controller and
// Classic Controller Pro, and the NES Classic and SNES Classic controllers,
// through Adafruit's Wii Nunchuck Breakout Adapter on STEMMA QT
// (extras/CONSOLES_PLAN.md, "I2C controllers").
//
// Written from the public protocol (WiiBrew, "Wiimote/Extension
// Controllers"); pico-infonesPlus's wiipad.cpp and NintendoExtensionCtrl
// were read for behaviour, not copied.
//
// THE PROTOCOL, briefly. The controller is I2C device 0x52. Writing 0x55 to
// register 0xF0 and then 0x00 to 0xFB starts it unencrypted. Writing a
// register address starts a read there; the data is ready ~200 us later.
// 0xFA holds a 6-byte identity (bytes 2-3 are A4 20 for every extension;
// Classic-family: xx 00 A4 20 mm 01, where mm is the report format).
// 0x00 holds the report. Buttons are active LOW.
//
// TWO REPORT FORMATS. Standard: 6 bytes, buttons in bytes 4-5. "High
// resolution" (0xFE = 0x03, what the NES/SNES Classic consoles use): 8
// bytes, buttons in bytes 6-7, the same bit layout. Genuine controllers do
// both; knockoff Classic Controllers only standard, knockoff NES Classic
// controllers only high resolution, and either may ignore a request to
// switch (NintendoExtensionCtrl's notes). So this asks for high resolution,
// always reads 8 bytes, and decides per read: in standard format bytes 6-7
// are zero, while in high resolution byte 6 bit 0 is always 1.
//
// NOTHING HERE BLOCKS for more than one I2C transaction. Start-up and
// hot-plug retries are a state machine driven by wii_classic_service(), so
// a game can call it every frame with the controller absent, present, or
// being plugged in.
#ifndef WII_CLASSIC_H
#define WII_CLASSIC_H

#include <stdint.h>
#include <stdbool.h>

class TwoWire;

// Buttons, active HIGH, as wii_classic_buttons() reports them.
enum {
    WII_BTN_UP     = 1u << 0,
    WII_BTN_DOWN   = 1u << 1,
    WII_BTN_LEFT   = 1u << 2,
    WII_BTN_RIGHT  = 1u << 3,
    WII_BTN_A      = 1u << 4,
    WII_BTN_B      = 1u << 5,
    WII_BTN_X      = 1u << 6,
    WII_BTN_Y      = 1u << 7,
    WII_BTN_L      = 1u << 8,  // L trigger, full press (the SNES Classic's L)
    WII_BTN_R      = 1u << 9,  // R trigger, full press
    WII_BTN_ZL     = 1u << 10,
    WII_BTN_ZR     = 1u << 11,
    WII_BTN_PLUS   = 1u << 12, // Start
    WII_BTN_MINUS  = 1u << 13, // Select
    WII_BTN_HOME   = 1u << 14,
};
#define WII_BTN_COUNT 15

typedef enum {
    WII_STATE_ABSENT = 0,  // nothing answered; retrying every second
    WII_STATE_STARTING,    // start-up sequence in progress
    WII_STATE_READY,       // reporting buttons
} wii_classic_state_t;

typedef struct {
    // Configuration.
    TwoWire *wire;

    // State, for the driver.
    wii_classic_state_t state;
    uint8_t  step;           // start-up step
    uint32_t step_at_ms;     // when the current step may run
    bool     requested;      // a report read is pending (0x00 written)

    // What the controller last said, for reporting.
    uint8_t  id[6];          // identity (register 0xFA)
    uint8_t  raw[8];         // last report, as read
    bool     hires;          // the last report was high resolution
    uint16_t buttons;        // WII_BTN_* held, from the last good report

    // Counters since begin().
    uint32_t connects;       // times it reached READY
    uint32_t drops;          // times a READY controller stopped answering

    // Diagnostics: failures per start-up step (index 0 = the first write,
    // 0xF0) and, for the latest failure, the step and the Wire result
    // (endTransmission()'s code, or requestFrom()'s count; for step 4, 0xA4
    // means the identity was read but didn't match).
    uint32_t step_fails[5];
    uint8_t  last_fail_step;
    uint8_t  last_fail_code;
} wii_classic_t;

// Sets up; the controller is found by wii_classic_service() later. The bus
// must already be begun (Wire.begin()) on the board's pins.
void wii_classic_begin(wii_classic_t *pad, TwoWire *wire);

// Runs start-up and retries. Call once per frame; cheap when READY.
void wii_classic_service(wii_classic_t *pad, uint32_t now_ms);

// The report, split in two so the ~200 us wait can be spent on other work:
// request() writes the register address (the controller then prepares the
// data), collect() reads it and updates `buttons`. Each is one short I2C
// transaction. A failed transaction drops the controller back to ABSENT
// with no buttons held. Both do nothing unless READY.
void wii_classic_request(wii_classic_t *pad);
void wii_classic_collect(wii_classic_t *pad);

static inline uint16_t wii_classic_buttons(const wii_classic_t *pad) {
    return pad->state == WII_STATE_READY ? pad->buttons : 0;
}

const char *wii_classic_button_name(unsigned bit_index);

#endif
