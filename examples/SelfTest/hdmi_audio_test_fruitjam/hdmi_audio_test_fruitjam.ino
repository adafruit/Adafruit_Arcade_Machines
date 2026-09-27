// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// hdmi_audio_test_fruitjam -- the spike for HDMI audio on the Fruit Jam
// (extras/HDMI_AUDIO_PLAN.md, step 1). Needs the Adafruit DVI Audio library
// (github.com/mikeysklar/Adafruit_DVI_Audio), which wraps pico_hdmi on the
// RP2350's HSTX, and a TV or HDMI monitor that plays sound from the cable.
//
// It answers the questions the backend swap depends on, before any game is
// touched:
//
//   - Does pico_hdmi's pull-model video run from OUR kind of scanline
//     queue? Core 0 renders 320-pixel lines into a small queue, the way the
//     machines feed hal_video; core 1's per-line callback pops one for every
//     two output lines and doubles it to 640. Missed lines are counted.
//   - Does the TV play 44.1 kHz audio made by doubling a 22,050 Hz stream
//     (the rate every machine here produces)? A tone plays; a background
//     task on core 1 makes it, repeats each sample, encodes 4-frame packets
//     and queues them.
//   - What does it cost? The callback's time per line and in total, and the
//     encode time per packet, both on core 1.
//
// The clock is ours, not the wrapper's begin(): 1.15 V (pico_hdmi's setting
// for 252 MHz), then fruitjam_set_sys_clock_khz(), which also retimes the
// PSRAM (DEVNOTES #129).
//
// On screen: colour bars with a white bar sweeping down, one line per frame.
// Serial, once a second: frames, missed and out-of-order lines, callback
// time, audio packets and encode time, and the audio queue's lowest level.
#include <Adafruit_Arcade_Machines.h>
#include <boards/fruitjam/board_config_fruitjam.h>
#include <Adafruit_DVI_Audio.h>   // pico_hdmi's C API (video_output.h, packets, queue)
#include "hardware/vreg.h"

#define SRC_W        320
#define SRC_H        240
#define QUEUE_LINES  16u          // power of two
#define AUDIO_RATE   44100u       // = 2 x 22,050
#define AUDIO_TARGET 200u         // queued packets, as pico_hdmi's example keeps

// --- The line queue: core 0 fills, core 1's video callback drains ---------

static uint16_t          s_lines[QUEUE_LINES][SRC_W];
static uint16_t          s_line_row[QUEUE_LINES];   // which source row each holds
static volatile uint32_t s_head = 0;                // written by core 0
static volatile uint32_t s_tail = 0;                // written by core 1

// Callback statistics (core 1, in the video interrupt).
static volatile uint32_t s_missed = 0;       // no line ready when needed
static volatile uint32_t s_out_of_order = 0; // the line ready was not the row due
static volatile uint32_t s_cb_us_sum = 0, s_cb_us_max = 0;

static void __not_in_flash_func(scanline_cb)(uint32_t v_scanline, uint32_t active_line,
                                             uint32_t *dst) {
    (void)v_scanline;
    const uint32_t t0 = time_us_32();
    const uint32_t row = active_line >> 1;
    static const uint16_t *cur = s_lines[0];
    if ((active_line & 1u) == 0) {
        // A new source row: take the next queued line, if there is one.
        const uint32_t tail = s_tail;
        if (tail != s_head) {
            const uint32_t slot = tail & (QUEUE_LINES - 1u);
            if (s_line_row[slot] != row) s_out_of_order++;
            cur = s_lines[slot];
        } else {
            s_missed++;                // repeat the previous line
        }
    }
    for (int i = 0; i < SRC_W; i++) {
        const uint32_t p = cur[i];
        dst[i] = p | (p << 16);
    }
    // The odd output line is the row's second and last use: free the slot.
    if ((active_line & 1u) == 1 && s_tail != s_head) s_tail = s_tail + 1;
    const uint32_t us = time_us_32() - t0;
    s_cb_us_sum += us;
    if (us > s_cb_us_max) s_cb_us_max = us;
}

// --- Audio: a background task in core 1's loop ----------------------------

static volatile uint32_t s_packets = 0, s_enc_us_sum = 0, s_enc_us_max = 0;
static volatile uint32_t s_queue_min = 0xFFFFFFFFu;
static int s_channel_frame = 0;

// A 440 Hz tone at 22,050 Hz, quiet (TV speakers), from a phase accumulator.
static int16_t next_sample_22k(void) {
    static uint32_t phase = 0;
    phase += (uint32_t)(440.0 * 4294967296.0 / 22050.0);
    // Triangle from the phase: cheap, and clearly audible as a tone.
    int32_t t = (int32_t)(phase >> 16) - 32768;          // -32768..32767 saw
    int32_t tri = (t < 0 ? -t : t) * 2 - 32768;          // triangle
    return (int16_t)(tri / 8);                           // about -18 dBFS
}

static void __not_in_flash_func(audio_task)(void) {
    const uint32_t level = hstx_di_queue_get_level();
    if (level < s_queue_min) s_queue_min = level;
    uint32_t budget = 8;   // packets per call, so the loop stays responsive
    while (budget-- && hstx_di_queue_get_level() < AUDIO_TARGET) {
        const uint32_t t0 = time_us_32();
        // Two 22,050 Hz samples, each repeated: four 44.1 kHz stereo frames.
        audio_sample_t s[4];
        for (int k = 0; k < 2; k++) {
            const int16_t v = next_sample_22k();
            s[2 * k].left = s[2 * k].right = v;
            s[2 * k + 1].left = s[2 * k + 1].right = v;
        }
        hstx_packet_t packet;
        const int next = hstx_packet_set_audio_samples_cs_rate(&packet, s, 4, s_channel_frame,
                                                               AUDIO_RATE);
        hstx_data_island_t island;
        hstx_encode_data_island(&island, &packet, false, DI_HSYNC_ACTIVE);
        if (!hstx_di_queue_push(&island)) break;
        s_channel_frame = next;
        const uint32_t us = time_us_32() - t0;
        s_packets = s_packets + 1;
        s_enc_us_sum = s_enc_us_sum + us;
        if (us > s_enc_us_max) s_enc_us_max = us;
    }
}

// --- Setup ----------------------------------------------------------------

static volatile bool s_video_ready = false;

void setup() {
    Serial.begin(115200);
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    delay(10);
    fruitjam_set_sys_clock_khz(252000);

    hstx_di_queue_init();
    video_output_init(MODE_H_ACTIVE_PIXELS, MODE_V_ACTIVE_LINES);
    pico_hdmi_set_audio_sample_rate(AUDIO_RATE);
    video_output_set_scanline_callback(scanline_cb);
    video_output_set_background_task(audio_task);
    s_video_ready = true;
}

void setup1() {
    while (!s_video_ready) tight_loop_contents();
    video_output_core1_run();   // never returns
}

// Core 0: render test lines into the queue, in order, as fast as it takes.
void loop() {
    static const uint16_t kBars[8] = { 0xFFFF, 0xFFE0, 0x07FF, 0x07E0,
                                       0xF81F, 0xF800, 0x001F, 0x0000 };
    static uint32_t frame = 0, row = 0, last_ms = 0, frames_1s = 0;

    while (s_head - s_tail >= QUEUE_LINES) tight_loop_contents();  // queue full
    const uint32_t slot = s_head & (QUEUE_LINES - 1u);
    uint16_t *line = s_lines[slot];
    const bool sweep = row == (frame % SRC_H);
    for (int x = 0; x < SRC_W; x++) line[x] = sweep ? 0xFFFF : kBars[x / (SRC_W / 8)];
    s_line_row[slot] = (uint16_t)row;
    s_head = s_head + 1;
    if (++row == SRC_H) { row = 0; frame++; frames_1s++; }

    const uint32_t now = millis();
    if (now - last_ms >= 1000) {
        last_ms = now;
        const uint32_t pk = s_packets, enc = s_enc_us_sum;
        Serial.printf("[hdmi-audio] %lu fps, missed %lu, out of order %lu; callback %lu us/s "
                      "(max %lu us/line); audio %lu packets/s (%lu Hz), encode %lu us/s "
                      "(max %lu us/packet), queue min %lu\n",
                      (unsigned long)frames_1s, (unsigned long)s_missed,
                      (unsigned long)s_out_of_order, (unsigned long)s_cb_us_sum,
                      (unsigned long)s_cb_us_max, (unsigned long)pk,
                      (unsigned long)(pk * 4u), (unsigned long)enc,
                      (unsigned long)s_enc_us_max, (unsigned long)s_queue_min);
        frames_1s = 0;
        s_missed = s_out_of_order = 0;
        s_cb_us_sum = s_cb_us_max = 0;
        s_packets = 0; s_enc_us_sum = 0; s_enc_us_max = 0;
        s_queue_min = 0xFFFFFFFFu;
    }
}
