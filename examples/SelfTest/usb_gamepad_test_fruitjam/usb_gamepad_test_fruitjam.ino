// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// USB GAMEPAD TEST -- what a USB controller on the Fruit Jam's Type-A ports
// reports, and what running the USB host costs core 0.
//
// Needs the Adafruit TinyUSB Library (bundled with the arduino-pico core)
// and the Pico PIO USB library (Library Manager), with Tools > USB Stack >
// Adafruit TinyUSB (sketch.yaml sets it). extras/CONSOLES_PLAN.md, "USB
// gamepads on the Fruit Jam", steps 1 and 2.
//
// The host runs the way the games will run it: on core 0 (core 1 is video
// in a game), at 252 MHz, on PIO 2 (video uses PIO 0, audio PIO 1), with a
// DMA channel no one else has claimed.
//
// It prints:
//   - core 0's spare capacity: a fixed chunk of work is timed for three
//     seconds BEFORE the host starts, then continuously after, so the
//     report says what share of core 0 the host's 1 ms interrupt takes,
//     idle and with a controller plugged in;
//   - on plug-in: vendor/product ID, the full HID report descriptor in hex,
//     and how the driver (src/input/usb_gamepad) will read it: which known
//     controller it is, or what the descriptor parser found;
//   - on every change of a controller's decoded buttons (or of its raw
//     bytes, with USB_TEST_RAW 1): the raw bytes, with the bytes that
//     changed since the last one printed marked, and the standard buttons
//     the driver decoded them to (by position: SOUTH is the bottom
//     face button, EAST the right one).
//
// A controller the driver can't read yet still has its descriptor and
// reports printed, which is what adding it to the table needs.
//
// Press one button at a time and hold it about a second, so each report
// change is one button.
#include <Adafruit_Arcade_Machines.h>
#include <Adafruit_TinyUSB.h>
#include <pio_usb.h>   // so the builder finds the Pico PIO USB library
#include <input/usb_gamepad.h>
#include <boards/fruitjam/usb_host_fruitjam.h>
#include <boards/fruitjam/board_config_fruitjam.h>

// --- core 0 capacity -------------------------------------------------------
// One "chunk" is a fixed amount of arithmetic; how many fit in 100 ms is
// the capacity. Interrupts that steal time show up as fewer chunks.
//
// Once the host is running, its task runs about every millisecond inside
// the loop, as a game would call it several times a frame. Calling it only
// between 100 ms windows made plugging in take ~10 s (each enumeration step
// waited for the next call), and the task's own cost belongs in the
// measurement anyway.
static volatile uint32_t g_sink;
static bool g_host_started = false;
static uint32_t g_host_started_ms = 0;
static uint32_t chunks_in_100ms(void) {
    const uint32_t t0 = micros();
    uint32_t n = 0, x = 1, last_task = t0;
    while (micros() - t0 < 100000u) {
        for (int i = 0; i < 1000; i++) x = x * 1664525u + 1013904223u;
        n++;
        if (g_host_started && micros() - last_task >= 1000u) {
            fruitjam_usb_host_task();
            last_task = micros();
        }
    }
    g_sink = x;
    return n;
}
static uint32_t g_baseline = 0;

extern "C" {
// Device-level plug-in and unplug, with the time since the host started
// (the driver uses only the HID-level callbacks, so these are free).
void tuh_mount_cb(uint8_t dev_addr) {
    Serial.print("[usb] device ");
    Serial.print(dev_addr);
    Serial.print(" connected ");
    Serial.print(millis() - g_host_started_ms);
    Serial.println(" ms after the host started");
}
void tuh_umount_cb(uint8_t dev_addr) {
    Serial.print("[usb] device ");
    Serial.print(dev_addr);
    Serial.print(" unplugged at ");
    Serial.print(millis() - g_host_started_ms);
    Serial.println(" ms");
}
}

static void print_hex(const uint8_t *b, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) {
        if (b[i] < 0x10) Serial.print('0');
        Serial.print(b[i], HEX);
        Serial.print(((i + 1) % 32 == 0 && i + 1 < n) ? "\n      " : " ");
    }
    Serial.println();
}

static void print_buttons(uint32_t b) {
    if (!b) { Serial.print("(none)"); return; }
    bool first = true;
    for (unsigned i = 0; i < USB_PAD_BUTTON_COUNT; i++) {
        if (!(b & (1u << i))) continue;
        if (!first) Serial.print('+');
        Serial.print(usb_pad_button_name(i));
        first = false;
    }
}

static void on_mount(uint8_t dev_addr, uint8_t inst, uint16_t vid, uint16_t pid,
                     const uint8_t *desc, uint16_t len, const usb_pad_layout_t *L, int player) {
    Serial.print("[usb] HID device ");
    Serial.print(dev_addr);
    Serial.print(" interface ");
    Serial.print(inst);
    Serial.print(": VID ");
    Serial.print(vid, HEX);
    Serial.print(" PID ");
    Serial.print(pid, HEX);
    Serial.print(", report descriptor ");
    Serial.print(len);
    Serial.println(" bytes:");
    Serial.print("      ");
    print_hex(desc, len);
    Serial.print("[usb]   -> ");
    if (player < 0) {
        Serial.println(L->kind == USB_PAD_UNSUPPORTED
                       ? "NOT READABLE as a gamepad (no usable descriptor, not in the table)"
                       : "no free player slot");
        return;
    }
    Serial.print("player ");
    Serial.print(player + 1);
    Serial.print(", ");
    Serial.print(L->name);
    if (L->kind == USB_PAD_GENERIC) {
        Serial.print(" (descriptor: ");
        Serial.print(L->n_buttons);
        Serial.print(" buttons");
        if (L->has_hat) Serial.print(", hat switch");
        if (L->has_x && L->has_y) Serial.print(", X/Y axes");
        if (L->report_id) { Serial.print(", report ID "); Serial.print(L->report_id); }
        Serial.print(")");
    } else {
        Serial.print(" (fixed layout)");
    }
    Serial.println();
}

// USB_TEST_RAW 1 prints every report whose bytes changed -- what adding a
// new controller needs. The default, 0, prints only when the decoded
// buttons change: a DualShock 4 changes bytes every report (its motion
// sensors), and printing ~250 of those lines a second costs core 0 about
// 12% on its own, which would swamp the measurement of the host.
#ifndef USB_TEST_RAW
#define USB_TEST_RAW 0
#endif
static volatile uint32_t g_reports;

static void on_report(int player, const uint8_t *rep, uint16_t len, uint32_t buttons) {
    static uint8_t last[USB_GAMEPAD_MAX][64];
    static uint16_t last_len[USB_GAMEPAD_MAX];
    static uint32_t last_buttons[USB_GAMEPAD_MAX];
    g_reports++;
    if (player < 0 || player >= USB_GAMEPAD_MAX) return;
    const uint16_t n = len < 64 ? len : 64;
    if (last_len[player] == n && memcmp(last[player], rep, n) == 0) return;
    if (!USB_TEST_RAW && last_len[player] && buttons == last_buttons[player]) {
        memcpy(last[player], rep, n);
        return;
    }
    last_buttons[player] = buttons;
    Serial.print("[usb] P");
    Serial.print(player + 1);
    Serial.print(" ");
    for (uint16_t i = 0; i < n; i++) {
        const bool changed = last_len[player] == n && last[player][i] != rep[i];
        Serial.print(changed ? '[' : ' ');
        if (rep[i] < 0x10) Serial.print('0');
        Serial.print(rep[i], HEX);
        Serial.print(changed ? ']' : ' ');
    }
    Serial.print("  = ");
    print_buttons(buttons);
    Serial.println();
    last_len[player] = n;
    memcpy(last[player], rep, n);
}

static void start_host(void) {
    usb_gamepad_set_hooks(on_mount, on_report);
    g_host_started = fruitjam_usb_host_begin();
    g_host_started_ms = millis();
    Serial.print("[usb] host ");
    Serial.print(g_host_started ? "started" : "FAILED to start");
    Serial.print(" on PIO 2, DMA channel ");
    Serial.print(fruitjam_usb_host_dma_channel());
    Serial.print(", clk_sys ");
    Serial.print(clock_get_hz(clk_sys) / 1000000u);
    Serial.println(" MHz");
}

void setup() {
    // The games' clock; PIO-USB needs a multiple of 12 MHz, and 252 is one.
    fruitjam_set_sys_clock_khz(252000);
    Serial.begin(115200);
}

void loop() {
    static uint32_t phase_start = millis(), base_sum = 0, base_n = 0;
    static uint32_t cap_sum = 0, cap_n = 0, cap_min = 0xFFFFFFFFu, last_report = 0;

    if (!g_host_started) {
        // Baseline: three seconds with no USB host running.
        base_sum += chunks_in_100ms(); base_n++;
        if (millis() - phase_start >= 3000u) {
            g_baseline = base_sum / base_n;
            Serial.print("[usb] baseline, no host: ");
            Serial.print(g_baseline);
            Serial.println(" chunks per 100 ms");
            start_host();
        }
        return;
    }

    const uint32_t c = chunks_in_100ms();
    cap_sum += c; cap_n++;
    if (c < cap_min) cap_min = c;

    if (millis() - last_report >= 2000u) {
        last_report = millis();
        const uint32_t mean = cap_sum / cap_n;
        Serial.print("[usb] core 0 with host: mean ");
        Serial.print(mean);
        Serial.print(" worst ");
        Serial.print(cap_min);
        Serial.print(" of baseline ");
        Serial.print(g_baseline);
        Serial.print(" chunks/100ms -> host takes ");
        Serial.print(g_baseline ? 100.0f * (float)(g_baseline - (mean < g_baseline ? mean : g_baseline)) / g_baseline : 0.0f, 1);
        Serial.print("% mean, ");
        Serial.print(g_baseline ? 100.0f * (float)(g_baseline - (cap_min < g_baseline ? cap_min : g_baseline)) / g_baseline : 0.0f, 1);
        Serial.print("% worst 100 ms; players");
        for (uint8_t p = 0; p < USB_GAMEPAD_MAX; p++) {
            usb_gamepad_info_t info;
            if (!usb_gamepad_info(p, &info)) break;
            Serial.print(' ');
            Serial.print(p + 1);
            Serial.print('=');
            Serial.print(info.name);
        }
        Serial.print(", reports/s ");
        Serial.print(g_reports / 2u);
        g_reports = 0;
        Serial.print(", unreadable HID devices ");
        Serial.println(usb_gamepad_unsupported_count());
        cap_sum = cap_n = 0; cap_min = 0xFFFFFFFFu;
    }
}
