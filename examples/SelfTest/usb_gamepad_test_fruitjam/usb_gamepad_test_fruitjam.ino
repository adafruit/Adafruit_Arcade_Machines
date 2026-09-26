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
//   - on plug-in: vendor/product ID, the HID interface's protocol, and
//     the full HID report descriptor, in hex;
//   - on every change of a controller's report: the raw bytes, with the
//     bytes that changed since the last report marked.
//
// Press one button at a time and hold it about a second, so each report
// change is one button.
#include <Adafruit_Arcade_Machines.h>
#include <Adafruit_TinyUSB.h>
#include <pio_usb.h>
#include <hardware/dma.h>
#include <boards/fruitjam/board_config_fruitjam.h>

static Adafruit_USBH_Host USBHost;

#define USB_PIO 2

// --- core 0 capacity -------------------------------------------------------
// One "chunk" is a fixed amount of arithmetic; how many fit in 100 ms is
// the capacity. Interrupts that steal time show up as fewer chunks.
static volatile uint32_t g_sink;
static uint32_t chunks_in_100ms(void) {
    const uint32_t t0 = micros();
    uint32_t n = 0, x = 1;
    while (micros() - t0 < 100000u) {
        for (int i = 0; i < 1000; i++) x = x * 1664525u + 1013904223u;
        n++;
    }
    g_sink = x;
    return n;
}
static uint32_t g_baseline = 0;

// --- USB -------------------------------------------------------------------
static bool g_host_started = false;

static void start_host(void) {
    pinMode(PIN_5V_EN, OUTPUT);
    digitalWrite(PIN_5V_EN, PIN_5V_EN_STATE); // power the Type-A ports

    pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp = PIN_USB_HOST_DP;
    cfg.pio_tx_num = USB_PIO;
    cfg.pio_rx_num = USB_PIO;
    // Pico PIO USB claims the channel it is given (dma_claim_mask), and
    // defaults to channel 0; hand it one that is free.
    const int ch = dma_claim_unused_channel(true);
    dma_channel_unclaim(ch);
    cfg.tx_ch = (uint8_t)ch;
    USBHost.configure_pio_usb(1, &cfg);
    USBHost.begin(1);
    g_host_started = true;
    Serial.print("[usb] host started on PIO ");
    Serial.print(USB_PIO);
    Serial.print(", DMA channel ");
    Serial.print(ch);
    Serial.print(", clk_sys ");
    Serial.print(clock_get_hz(clk_sys) / 1000000u);
    Serial.println(" MHz");
}

static void print_hex(const uint8_t *b, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) {
        if (b[i] < 0x10) Serial.print('0');
        Serial.print(b[i], HEX);
        Serial.print(((i + 1) % 32 == 0 && i + 1 < n) ? "\n      " : " ");
    }
    Serial.println();
}

struct last_report_t { uint8_t dev, inst; uint16_t len; uint8_t data[64]; };
static last_report_t g_last[4];

extern "C" {

void tuh_mount_cb(uint8_t dev_addr) {
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    Serial.print("[usb] device ");
    Serial.print(dev_addr);
    Serial.print(" mounted: VID ");
    Serial.print(vid, HEX);
    Serial.print(" PID ");
    Serial.println(pid, HEX);
}

void tuh_umount_cb(uint8_t dev_addr) {
    Serial.print("[usb] device ");
    Serial.print(dev_addr);
    Serial.println(" unmounted");
    for (auto &l : g_last) if (l.dev == dev_addr) l = last_report_t{};
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t inst, const uint8_t *desc, uint16_t len) {
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    const uint8_t proto = tuh_hid_interface_protocol(dev_addr, inst);
    Serial.print("[usb] HID device ");
    Serial.print(dev_addr);
    Serial.print(" interface ");
    Serial.print(inst);
    Serial.print(": VID ");
    Serial.print(vid, HEX);
    Serial.print(" PID ");
    Serial.print(pid, HEX);
    Serial.print(", protocol ");
    Serial.print(proto == HID_ITF_PROTOCOL_KEYBOARD ? "keyboard"
                 : proto == HID_ITF_PROTOCOL_MOUSE ? "mouse" : "none (gamepad, joystick, ...)");
    Serial.print(", report descriptor ");
    Serial.print(len);
    Serial.println(" bytes:");
    Serial.print("      ");
    print_hex(desc, len);
    if (!tuh_hid_receive_report(dev_addr, inst)) Serial.println("[usb] cannot request reports");
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t inst) {
    Serial.print("[usb] HID device ");
    Serial.print(dev_addr);
    Serial.print(" interface ");
    Serial.print(inst);
    Serial.println(" unmounted");
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t inst, const uint8_t *rep, uint16_t len) {
    last_report_t *slot = nullptr;
    for (auto &l : g_last) if (l.len && l.dev == dev_addr && l.inst == inst) slot = &l;
    if (!slot) for (auto &l : g_last) if (!l.len) { slot = &l; break; }
    const uint16_t n = len < sizeof slot->data ? len : sizeof slot->data;
    if (slot && !(slot->len == n && memcmp(slot->data, rep, n) == 0)) {
        Serial.print("[usb] ");
        Serial.print(dev_addr);
        Serial.print('.');
        Serial.print(inst);
        Serial.print(" report ");
        Serial.print(len);
        Serial.print("B: ");
        for (uint16_t i = 0; i < n; i++) {
            const bool changed = slot->len == n && slot->data[i] != rep[i];
            Serial.print(changed ? '[' : ' ');
            if (rep[i] < 0x10) Serial.print('0');
            Serial.print(rep[i], HEX);
            Serial.print(changed ? ']' : ' ');
        }
        Serial.println();
        slot->dev = dev_addr; slot->inst = inst; slot->len = n;
        memcpy(slot->data, rep, n);
    }
    tuh_hid_receive_report(dev_addr, inst);
}

} // extern "C"

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

    USBHost.task();
    const uint32_t c = chunks_in_100ms();
    USBHost.task();
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
        Serial.println("% worst 100 ms");
        cap_sum = cap_n = 0; cap_min = 0xFFFFFFFFu;
    }
}
