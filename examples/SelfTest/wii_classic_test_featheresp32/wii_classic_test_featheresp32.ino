// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// WII CLASSIC CONTROLLER TEST, FEATHER ESP32 V2 -- what a Wii
// Classic-protocol controller on the STEMMA QT port reports, and what
// polling it costs. The Feather version of wii_classic_test_fruitjam, made
// to try third-party controllers the way the games really drive them: the
// same driver (input/wii_classic.h) at the same 100 kHz
// (wii_input_feather_esp32.cpp).
//
// Plug the controller into Adafruit's Wii Nunchuck Breakout Adapter and the
// adapter into the Feather's STEMMA QT port. Open serial at 115200; opening
// the port resets this board. Plugging and unplugging while it runs is
// fine; that is part of the test. Nothing else is needed: no display, card
// or button panel.
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
// 200 us wait), collect. Build with -DWII_TEST_CLOCK=400000 to compare the
// faster rate, which gave all-FF reports from a first-party pad on this
// board (DEVNOTES #143).
//
// TWO MODES. By default it reads through the ESP32's I2C hardware and the
// driver, exactly as the games do. Built with -DWII_TEST_BITBANG it skips
// both and drives SDA/SCL by hand ("software I2C"): slow and forgiving,
// for a controller the hardware can't read. It prints every distinct
// report, including ones that fail the high-resolution check (byte 6 bit
// 0 set), and how long the controller held SCL before each byte.
//
// Switches, all -D build flags (arduino-cli: --build-property
// "compiler.cpp.extra_flags=-DWII_TEST_BITBANG -DWII_TEST_PERIOD_MS=50"):
//   WII_TEST_CLOCK=Hz     hardware mode's I2C clock (default 100000)
//   WII_TEST_GAP_US=us    wait between request and read (default 200)
//   WII_TEST_PERIOD_MS=ms time between polls (default 16)
//   WII_TEST_BITBANG      software I2C, as above
//   WII_TEST_NO_HIRES     software mode: don't ask for high resolution
//   WII_TEST_OLD_INIT     software mode: the original 0x40=0x00 start-up,
//                         printing the decrypted bytes as well
//   WII_TEST_NO_POINTER   software mode: read without writing 0x00 first
// What they found on a third-party SNES Classic replica: DEVNOTES #162.
//
// No display, no audio: input only (extras/CONSOLES_PLAN.md, "I2C
// controllers"; DEVNOTES #162).
#include <Adafruit_Arcade_Machines.h>
#include <Wire.h>
#include <input/wii_classic.h>

#ifndef WII_TEST_CLOCK
#define WII_TEST_CLOCK 100000   // what the Feather's games use
#endif
// The wait between asking for a report and reading it. The games leave at
// least a millisecond (one input-task tick); some clones need more.
#ifndef WII_TEST_GAP_US
#define WII_TEST_GAP_US 200
#endif

static wii_classic_t g_pad;

struct stat_t { uint32_t sum, max, n; };
static void add(stat_t &s, uint32_t us) { s.sum += us; if (us > s.max) s.max = us; s.n++; }
static stat_t st_req, st_col, st_svc;

#ifdef WII_TEST_BITBANG
// The driver's high-resolution decode (bytes 6-7, active low), for raw bytes.
static uint16_t wii_classic_decode_hires_for_test(const uint8_t *raw) {
    const uint8_t h = (uint8_t)~raw[6], l = (uint8_t)~raw[7];
    uint16_t v = 0;
    if (l & 0x01) v |= WII_BTN_UP;    if (h & 0x40) v |= WII_BTN_DOWN;
    if (l & 0x02) v |= WII_BTN_LEFT;  if (h & 0x80) v |= WII_BTN_RIGHT;
    if (l & 0x10) v |= WII_BTN_A;     if (l & 0x40) v |= WII_BTN_B;
    if (l & 0x08) v |= WII_BTN_X;     if (l & 0x20) v |= WII_BTN_Y;
    if (h & 0x20) v |= WII_BTN_L;     if (h & 0x02) v |= WII_BTN_R;
    if (l & 0x80) v |= WII_BTN_ZL;    if (l & 0x04) v |= WII_BTN_ZR;
    if (h & 0x04) v |= WII_BTN_PLUS;  if (h & 0x10) v |= WII_BTN_MINUS;
    if (h & 0x08) v |= WII_BTN_HOME;
    return v;
}
#endif
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

// Which 7-bit addresses ACK an empty write: 0x52 while a controller is
// plugged in, plus anything else on the STEMMA QT bus.
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

#ifdef WII_TEST_BITBANG
// --- Software I2C, for controllers the ESP32's I2C hardware can't read ----
//
// Open-drain by hand: a line is released (INPUT, the pull-up raises it) or
// pulled LOW (OUTPUT low). Every rise of SCL waits for the controller to
// let it go, as long as it likes, and the longest such wait before each
// byte is recorded: clock stretching the hardware may give up on.
static const int kSda = SDA, kScl = SCL;
#ifndef WII_TEST_PERIOD_MS
#define WII_TEST_PERIOD_MS 16
#endif
static uint32_t g_stretch_us[9];   // longest SCL hold per byte position
static inline void rel(int pin) { pinMode(pin, INPUT); }
static inline void low(int pin) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
static const uint32_t kHalfUs = 5;  // ~100 kHz
static bool scl_up(int byte_pos) {
    rel(kScl);
    const uint32_t t0 = micros();
    while (digitalRead(kScl) == LOW) {
        if (micros() - t0 > 100000u) return false;   // 100 ms: give up
    }
    const uint32_t w = micros() - t0;
    if (byte_pos >= 0 && byte_pos < 9 && w > g_stretch_us[byte_pos]) g_stretch_us[byte_pos] = w;
    delayMicroseconds(kHalfUs);
    return true;
}
static void bb_start(void) { rel(kSda); rel(kScl); delayMicroseconds(kHalfUs); low(kSda); delayMicroseconds(kHalfUs); low(kScl); }
static void bb_stop(void)  { low(kSda); delayMicroseconds(kHalfUs); rel(kScl); delayMicroseconds(kHalfUs); rel(kSda); delayMicroseconds(kHalfUs); }
static bool bb_write(uint8_t b) {   // true if ACKed
    for (int i = 7; i >= 0; i--) {
        if (b & (1u << i)) rel(kSda); else low(kSda);
        delayMicroseconds(kHalfUs);
        if (!scl_up(-1)) return false;
        low(kScl);
    }
    rel(kSda); delayMicroseconds(kHalfUs);
    if (!scl_up(-1)) return false;
    const bool ack = digitalRead(kSda) == LOW;
    low(kScl);
    return ack;
}
static bool bb_read(uint8_t *out, int pos, bool ack) {
    rel(kSda);
    uint8_t v = 0;
    for (int i = 7; i >= 0; i--) {
        delayMicroseconds(kHalfUs);
        if (!scl_up(i == 7 ? pos : -1)) return false;
        if (digitalRead(kSda)) v |= (uint8_t)(1u << i);
        low(kScl);
    }
    if (ack) low(kSda); else rel(kSda);
    delayMicroseconds(kHalfUs);
    if (!scl_up(-1)) return false;
    low(kScl); rel(kSda);
    *out = v;
    return true;
}
static bool bb_write_regs(const uint8_t *b, int n) {
    bb_start();
    bool ok = bb_write(0x52 << 1);
    for (int i = 0; ok && i < n; i++) ok = bb_write(b[i]);
    bb_stop();
    return ok;
}
static int bb_read_n(uint8_t *buf, int n) {   // bytes read, or -1 for no ACK
    bb_start();
    if (!bb_write((0x52 << 1) | 1)) { bb_stop(); return -1; }
    int got = 0;
    for (int i = 0; i < n; i++) {
        if (!bb_read(&buf[i], i, i + 1 < n)) break;
        got++;
    }
    bb_stop();
    return got;
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("[wii] Feather ESP32 V2 Wii Classic test -- SOFTWARE I2C");
    pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
    delay(100);
    rel(kSda); rel(kScl);
    const uint8_t a[] = { 0xF0, 0x55 }, b[] = { 0xFB, 0x00 }, c[] = { 0xFE, 0x03 }, fa[] = { 0xFA };
#ifdef WII_TEST_OLD_INIT
    // The original Wii init: 0x00 to register 0x40, data then "encrypted"
    // as x' = (x ^ 0x17) + 0x17 (WiiBrew).
    const uint8_t old_init[] = { 0x40, 0x00 };
    Serial.printf("[wii] OLD init 40=00 %s\n", bb_write_regs(old_init, 2) ? "ack" : "NACK");
    delay(20);
#else
    Serial.printf("[wii] init F0=55 %s, FB=00 %s",
                  bb_write_regs(a, 2) ? "ack" : "NACK", (delay(20), bb_write_regs(b, 2)) ? "ack" : "NACK");
#ifndef WII_TEST_NO_HIRES
    Serial.printf(", FE=03 %s", (delay(20), bb_write_regs(c, 2)) ? "ack" : "NACK");
#endif
    Serial.println();
#endif
    uint8_t id[6] = {0};
    bb_write_regs(fa, 1); delay(1);
    Serial.printf("[wii] identity read %d:", bb_read_n(id, 6));
    for (int i = 0; i < 6; i++) Serial.printf(" %02X", id[i]);
    Serial.println();
}

void loop() {
    static uint32_t ok = 0, bad = 0, nack = 0, last = 0;
    static uint8_t prev[8] = {1,2,3,4,5,6,7,8};
    const uint8_t z[] = { 0x00 };
    uint8_t r[8] = {0};
#ifndef WII_TEST_NO_POINTER
    if (!bb_write_regs(z, 1)) nack++;
#endif
    delayMicroseconds(WII_TEST_GAP_US);
    const int got = bb_read_n(r, 8);
    if (got == 8) ok++; else bad++;
    // A real high-resolution report always has byte 6 bit 0 set; this
    // clone sends 02 02 02 ... on every other read, which hasn't.
    static uint32_t junk = 0;
    static uint8_t prev_junk[8] = {1,2,3,4,5,6,7,8};
    if (got == 8 && !(r[6] & 0x01)) {
        junk++;
        if (memcmp(r, prev_junk, 8) != 0) {   // print these too: they may carry the buttons
            memcpy(prev_junk, r, 8);
            Serial.print("[wii] other");
            for (int i = 0; i < 8; i++) Serial.printf(" %02X", r[i]);
            Serial.printf("  (t %lu ms)\n", (unsigned long)millis());
        }
        delay(WII_TEST_PERIOD_MS);
        return;
    }
    if (got == 8 && memcmp(r, prev, 8) != 0) {
        memcpy(prev, r, 8);
        Serial.print("[wii] raw");
        for (int i = 0; i < 8; i++) Serial.printf(" %02X", r[i]);
#ifdef WII_TEST_OLD_INIT
        Serial.print("  decrypted");
        for (int i = 0; i < 8; i++) Serial.printf(" %02X", (uint8_t)((r[i] ^ 0x17) + 0x17));
#endif
        Serial.print("  buttons ");
        print_buttons(wii_classic_decode_hires_for_test(r));
        Serial.printf("  (t %lu ms)\n", (unsigned long)millis());
    }
    if (millis() - last >= 1000) {
        last = millis();
        Serial.printf("[wii] 8-byte reads ok %lu (junk %lu), short %lu, pointer NACKs %lu; longest clock stretch before byte 0..7 (us):",
                      (unsigned long)ok, (unsigned long)junk, (unsigned long)bad, (unsigned long)nack);
        for (int i = 0; i < 8; i++) Serial.printf(" %lu", (unsigned long)g_stretch_us[i]);
        Serial.println();
        memset(g_stretch_us, 0, sizeof g_stretch_us);
    }
    delay(WII_TEST_PERIOD_MS);
}
#else
void setup() {
    Serial.begin(115200);
    delay(1500); // the port opening resets the board; give the monitor time
    Serial.println("[wii] Feather ESP32 V2 Wii Classic test");
    // The STEMMA QT port's power is switched, and off at reset.
    pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
    delay(50);
    Wire.begin(SDA, SCL, WII_TEST_CLOCK);
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
        delayMicroseconds(WII_TEST_GAP_US); // stands in for a game's other work
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
        Serial.print(" kHz, gap ");
        Serial.print(WII_TEST_GAP_US);
        Serial.print(" us; request mean ");
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
        for (int i = 0; i < 7; i++) { Serial.print(' '); Serial.print(g_pad.step_fails[i]); }
        Serial.print(", last step ");
        Serial.print(g_pad.last_fail_step);
        Serial.print(" code ");
        Serial.println(g_pad.last_fail_code);
        static uint32_t scans = 0;
        if (scans++ % 5 == 0) scan_bus();
        st_req = st_col = st_svc = stat_t{};
    }

#ifndef WII_TEST_PERIOD_MS
#define WII_TEST_PERIOD_MS 16
#endif
    delay(WII_TEST_PERIOD_MS); // about once per frame by default
}
#endif // WII_TEST_BITBANG
