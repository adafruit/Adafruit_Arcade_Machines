// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// hal_video.h for the Adafruit Fruit Jam on the RP2350's HSTX, through
// pico_hdmi (via the Adafruit DVI Audio library). The alternative to
// hal_video_fruitjam.cpp's PicoDVI (PIO) backend, chosen at build time with
// -DARCADE_FRUITJAM_HSTX; see extras/HDMI_AUDIO_PLAN.md, step 2. It exists
// so the display can carry HDMI audio (step 3), and it frees PIO 0 and most
// of core 1, since HSTX encodes TMDS in hardware.
//
// SAME CONTRACT, SAME QUEUE. The machines render 320-pixel lines through
// acquire/submit into 32 buffers cycled through a free and a valid queue,
// exactly as with PicoDVI, so frame pacing, the statistics and the USB idle
// hook behave the same. What differs is the consumer: pico_hdmi's per-line
// interrupt on core 1 PULLS, calling scanline_cb() once per 640x480 output
// line. It takes a buffer from the valid queue for every second output line,
// doubles each pixel into the 640-pixel line, and after the second use puts
// the buffer back on the free queue.
//
// NOTHING THE CALLBACK TOUCHES MAY LIVE IN FLASH. The first version used
// the Pico SDK's queue_t, whose functions are in flash and take spin locks.
// With a game running from flash on core 0, a cache miss inside the video
// interrupt made it late, one HSTX command word went out wrong, and the
// stream desynchronised for good: a blank TV, with lines "completing" at
// bus speed (pico_hdmi's own video_output_force_resync() comment describes
// exactly this). So the two queues are lock-free single-producer rings,
// fully inlined, as in the spike that worked (DEVNOTES #147, #148).
//
// THE LINE ORDER HEALS ITSELF. The consumer cannot block the way PicoDVI's
// pump does, so a late line cannot simply be waited for. Each buffer
// carries the row it was submitted for; a stale row is dropped, a row that
// is early waits, and a row with nothing ready shows RED -- the same
// starvation signal as PicoDVI's -- so one missed line never shifts the
// rest of the picture. Measured in the spike first (DEVNOTES #147).
#if defined(ARDUINO_ADAFRUIT_FRUITJAM_RP2350) && defined(ARCADE_FRUITJAM_HSTX)

#include <Adafruit_DVI_Audio.h>   // pico_hdmi: video_output.h, the audio queue
#include "pico/platform.h"
#include "pico/time.h"
#include "hardware/vreg.h"
#include "hal/arcade_hal_video.h"
#include "boards/fruitjam/board_config_fruitjam.h"

const uint32_t HAL_VIDEO_WIDTH  = 320;
const uint32_t HAL_VIDEO_HEIGHT = 240;

#define N_SCANBUF 32          // the same runway as PicoDVI's (DEVNOTES #85)
#define HDMI_AUDIO_RATE 44100 // twice the machines' 22,050 Hz (step 3)

static uint16_t s_buf[N_SCANBUF][320];
static uint16_t s_row[N_SCANBUF];     // the canvas row each buffer holds

// A single-producer, single-consumer ring of buffer pointers, big enough
// for every buffer. `valid`: core 0 pushes, core 1's callback pops. `free`:
// the callback pushes, core 0 pops. No locks; a barrier orders each slot
// write before the index that publishes it.
typedef struct {
    uint16_t *slot[N_SCANBUF];
    volatile uint32_t head;   // written by the producer only
    volatile uint32_t tail;   // written by the consumer only
} ring_t;

static ring_t s_free, s_valid;

static __force_inline void ring_init(ring_t *r) { r->head = r->tail = 0; }
static __force_inline uint32_t ring_level(const ring_t *r) { return r->head - r->tail; }
static __force_inline bool ring_push(ring_t *r, uint16_t *b) {
    if (r->head - r->tail >= N_SCANBUF) return false;
    r->slot[r->head & (N_SCANBUF - 1u)] = b;
    __dmb();
    r->head = r->head + 1;
    return true;
}
static __force_inline bool ring_peek(const ring_t *r, uint16_t **b) {
    if (r->tail == r->head) return false;
    __dmb();
    *b = r->slot[r->tail & (N_SCANBUF - 1u)];
    return true;
}
static __force_inline void ring_drop(ring_t *r) {
    __dmb();
    r->tail = r->tail + 1;
}
static uint32_t s_next_row = 0;       // producer: the row the next submit is

static volatile uint32_t s_blocked_us = 0;
static volatile uint32_t s_starve_events = 0;
static volatile uint32_t s_min_valid = 0xFFFFFFFFu;
static void (*volatile s_idle_hook)(void) = nullptr;

void fruitjam_video_set_idle_hook(void (*hook)(void)) { s_idle_hook = hook; }

static inline uint32_t buf_index(const uint16_t *b) {
    return (uint32_t)(b - s_buf[0]) / 320u;
}

// --- Consumer: core 1, inside pico_hdmi's per-line interrupt ---------------

static uint16_t *s_cur = nullptr;     // the buffer being shown, or null
static bool      s_cur_owned = false; // it must go back to the free queue

static void __not_in_flash_func(release_cur)(void) {
    if (s_cur_owned) {
        (void)ring_push(&s_free, s_cur);
        s_cur_owned = false;
    }
    s_cur = nullptr;
}

static void __not_in_flash_func(scanline_cb)(uint32_t v_scanline, uint32_t active_line,
                                             uint32_t *dst) {
    (void)v_scanline;
    const uint32_t row = active_line >> 1;
    if ((active_line & 1u) == 0) {
        release_cur();
        // Take the buffer for this row: drop stale ones, leave an early one.
        uint16_t *b;
        while (ring_peek(&s_valid, &b)) {
            const uint32_t r = s_row[buf_index(b)];
            const uint32_t ahead = (r + HAL_VIDEO_HEIGHT - row) % HAL_VIDEO_HEIGHT;
            if (ahead == 0) {                          // this row
                ring_drop(&s_valid);
                s_cur = b; s_cur_owned = true;
                break;
            }
            if (ahead < HAL_VIDEO_HEIGHT / 2) break;   // early: keep it
            ring_drop(&s_valid);                       // stale: drop it
            (void)ring_push(&s_free, b);
        }
    }
    if (s_cur) {
        const uint16_t *src = s_cur;
        for (int i = 0; i < 320; i++) {
            const uint32_t p = src[i];
            dst[i] = p | (p << 16);
        }
    } else {
        for (int i = 0; i < 320; i++) dst[i] = 0xF800F800u;   // red: starved
    }
    if (active_line == MODE_V_ACTIVE_LINES - 1u) release_cur();
}

// --- The HAL ---------------------------------------------------------------

bool hal_video_init(void) {
    ring_init(&s_free);
    ring_init(&s_valid);
    for (int i = 0; i < N_SCANBUF; i++) (void)ring_push(&s_free, s_buf[i]);
    s_next_row = 0;
    // pico_hdmi's core voltage for 252 MHz HSTX. Here, not only in
    // fruitjam_set_sys_clock_khz(), because some sketches set the clock with
    // a bare set_sys_clock_khz() (galaga_fruitjam does).
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(10);
    // Configure only: the signal starts in hal_video_run(), as the HAL
    // requires (video_output_core1_run() enables HSTX and the DMA).
    hstx_di_queue_init();
    video_output_init(MODE_H_ACTIVE_PIXELS, MODE_V_ACTIVE_LINES);
    pico_hdmi_set_audio_sample_rate(HDMI_AUDIO_RATE);
    video_output_set_scanline_callback(scanline_cb);
    return true;
}

uint16_t *hal_video_acquire_scanline(void) {
    uint16_t *b;
    const uint32_t t0 = time_us_32();
    void (*hook)(void) = s_idle_hook;
    while (!ring_peek(&s_free, &b)) {
        if (hook) hook(); else tight_loop_contents();
    }
    ring_drop(&s_free);
    s_blocked_us += time_us_32() - t0;
    return b;
}

void hal_video_submit_scanline(uint16_t *buf) {
    s_row[buf_index(buf)] = (uint16_t)s_next_row;
    if (++s_next_row == HAL_VIDEO_HEIGHT) s_next_row = 0;
    while (!ring_push(&s_valid, buf)) tight_loop_contents();   // cannot fill: 32 slots
    const uint32_t lvl = ring_level(&s_valid);
    if (lvl <= 1u) s_starve_events++;
    if (lvl < s_min_valid) s_min_valid = lvl;
}

uint32_t hal_video_take_blocked_us(void) {
    const uint32_t v = s_blocked_us;
    s_blocked_us = 0;
    return v;
}

uint32_t hal_video_valid_level(void) { return ring_level(&s_valid); }

uint32_t hal_video_take_min_valid_level(void) {
    const uint32_t v = s_min_valid;
    s_min_valid = 0xFFFFFFFFu;
    return (v == 0xFFFFFFFFu) ? 0u : v;
}

uint32_t hal_video_scanbuf_count(void) { return (uint32_t)N_SCANBUF; }

uint32_t hal_video_take_starve_count(void) {
    const uint32_t v = s_starve_events;
    s_starve_events = 0;
    return v;
}

void hal_video_probe_readback(void) {}

void hal_video_run(void) {
    video_output_core1_run();   // never returns
    __builtin_unreachable();
}

#endif // ARDUINO_ADAFRUIT_FRUITJAM_RP2350 && ARCADE_FRUITJAM_HSTX
