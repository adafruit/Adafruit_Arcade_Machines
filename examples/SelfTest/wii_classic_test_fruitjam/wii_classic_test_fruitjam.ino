// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// WII CLASSIC CONTROLLER TEST -- what a Wii Classic-protocol controller on
// the STEMMA QT port reports, and what polling it costs.
//
// Plug the controller into Adafruit's Wii Nunchuck Breakout Adapter and the
// adapter into the Fruit Jam's STEMMA QT port (I2C on GPIO 20/21, shared
// with the audio DAC). Open serial at 115200. Plugging and unplugging
// while it runs is fine; that is part of the test.
//
// It prints:
//   - on connect: the identity bytes (register 0xFA) and the report format
//     the identity claims (01 standard, 03 high resolution);
//   - on every change of buttons: the buttons held, the raw 8-byte report,
//     and which format that report was decoded as;
//   - every 5 seconds: a scan of which I2C addresses answer;
//   - once a second: the time each part of a poll took (request = write
//     the register address, collect = read 8 bytes), mean and worst, and
//     the connect/drop counts, and failures per start-up step with the
//     latest Wire result code.
//
// The poll is done the way a game would: request, other work (here a
// 200 us wait), collect. Build with -DWII_TEST_CLOCK=100000 to measure the
// 100 kHz rate some clones need.
//
// No display, no audio: input only (extras/CONSOLES_PLAN.md, "I2C
// controllers", step 1).
#include <Adafruit_Arcade_Machines.h>
#include <Wire.h>
#include <input/wii_classic.h>
#include <boards/fruitjam/board_config_fruitjam.h>

#ifndef WII_TEST_CLOCK
#define WII_TEST_CLOCK 400000
#endif

static wii_classic_t g_pad;

struct stat_t { uint32_t sum, max, n; };
static void add(stat_t &s, uint32_t us) { s.sum += us; if (us > s.max) s.max = us; s.n++; }
static stat_t st_req, st_col, st_svc;

static void print_hex(const uint8_t *b, int n) {
    for (int i = 0; i < n; i++) {
        if (b[i] < 0x10) Serial.print('0');
        Serial.print(b[i], HEX);
        if (i + 1 < n) Serial.print(' ');
    }
}

static void print_buttons(uint16_t v) {
    if (!v) { Serial.print("(none)"); return; }
    bool first = true;
    for (unsigned i = 0; i < WII_BTN_COUNT; i++) {
        if (!(v & (1u << i))) continue;
        if (!first) Serial.print(' ');
        Serial.print(wii_classic_button_name(i));
        first = false;
    }
}

// Which 7-bit addresses ACK an empty write. The DAC (0x18) should always
// be there, and 0x52 only while a controller is plugged in.
static void scan_bus(void) {
    Serial.print("[wii] bus scan: ");
    int found = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            if (found++) Serial.print(' ');
            Serial.print("0x");
            Serial.print(a, HEX);
        }
    }
    Serial.println(found ? "" : "(nothing)");
}

void setup() {
    Serial.begin(115200);
    Wire.setSDA(FRUITJAM_I2C_SDA_PIN);
    Wire.setSCL(FRUITJAM_I2C_SCL_PIN);
    Wire.begin();
    Wire.setClock(WII_TEST_CLOCK);
    wii_classic_begin(&g_pad, &Wire);
}

void loop() {
    static wii_classic_state_t last_state = WII_STATE_ABSENT;
    static uint16_t last_buttons = 0xFFFF;
    static uint32_t last_report = 0;

    uint32_t t = micros();
    wii_classic_service(&g_pad, millis());
    add(st_svc, micros() - t);

    if (g_pad.state != last_state) {
        if (g_pad.state == WII_STATE_READY) {
            Serial.print("[wii] connected: identity ");
            print_hex(g_pad.id, 6);
            Serial.print(" -> ");
            Serial.print(g_pad.id[4] == 0x03 ? "claims high resolution"
                         : g_pad.id[4] == 0x01 ? "claims standard" : "claims format ??");
            Serial.println(g_pad.id[0] <= 1 && g_pad.id[1] == 0 && g_pad.id[5] == 0x01
                           ? " (Classic family)" : " (NOT a Classic-family identity)");
        } else if (last_state == WII_STATE_READY) {
            Serial.println("[wii] disconnected");
        }
        last_state = g_pad.state;
        last_buttons = 0xFFFF;
    }

    if (g_pad.state == WII_STATE_READY) {
        t = micros();
        wii_classic_request(&g_pad);
        add(st_req, micros() - t);
        delayMicroseconds(200); // stands in for a game's other work
        t = micros();
        wii_classic_collect(&g_pad);
        add(st_col, micros() - t);

        const uint16_t b = wii_classic_buttons(&g_pad);
        if (g_pad.state == WII_STATE_READY && b != last_buttons) {
            last_buttons = b;
            Serial.print("[wii] ");
            print_buttons(b);
            Serial.print("   raw ");
            print_hex(g_pad.raw, 8);
            Serial.println(g_pad.hires ? "  (read as high resolution)" : "  (read as standard)");
        }
    }

    const uint32_t now = millis();
    if (now - last_report >= 1000) {
        last_report = now;
        Serial.print("[wii] ");
        Serial.print(g_pad.state == WII_STATE_READY ? "ready" :
                     g_pad.state == WII_STATE_STARTING ? "starting" : "absent");
        Serial.print(", I2C ");
        Serial.print(WII_TEST_CLOCK / 1000);
        Serial.print(" kHz; request mean ");
        Serial.print(st_req.n ? st_req.sum / st_req.n : 0);
        Serial.print("us max ");
        Serial.print(st_req.max);
        Serial.print("us; collect mean ");
        Serial.print(st_col.n ? st_col.sum / st_col.n : 0);
        Serial.print("us max ");
        Serial.print(st_col.max);
        Serial.print("us; service max ");
        Serial.print(st_svc.max);
        Serial.print("us; polls ");
        Serial.print(st_col.n);
        Serial.print(", connects ");
        Serial.print(g_pad.connects);
        Serial.print(", drops ");
        Serial.print(g_pad.drops);
        Serial.print("; step fails");
        for (int i = 0; i < 5; i++) { Serial.print(' '); Serial.print(g_pad.step_fails[i]); }
        Serial.print(", last step ");
        Serial.print(g_pad.last_fail_step);
        Serial.print(" code ");
        Serial.println(g_pad.last_fail_code);
        static uint32_t scans = 0;
        if (scans++ % 5 == 0) scan_bus();
        st_req = st_col = st_svc = stat_t{};
    }

    delay(16); // about once per frame
}
