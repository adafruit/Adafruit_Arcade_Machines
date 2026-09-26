// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// CONSOLE SPEED SPIKE ON THE FEATHER ESP32 V2 -- how long the Game Boy and
// NES cores take to emulate one frame on the ESP32 at 240 MHz, before
// anything is ported (extras/CONSOLES_PLAN.md, Phase 4, step 1).
//
// Loads the first .gb or .nes in /cart on the SD card into PSRAM and runs it
// through the library's own core wrappers (machines/gb/gameboy_core,
// machines/nes/nes_core), headless, with the host harnesses' scripted input,
// and prints once a second the emulation time per frame (mean, worst) and
// the audio generation time. For the NES it also prints frame CRCs at
// frames 150, 650, 1200 and 1500, comparable with
//   nes_host --rom <SMB> --frames 1500 <same presses> --crc-at 150,650,1200,1500
//
// The Feather paints ~30 frames a second and shows 2 emulated frames per
// paint, so to fit, one frame of emulation plus audio must stay well under
// half of a ~33 ms paint -- the emulation runs on the other core, but the
// budget is still 16.7 ms a frame.
//
// No display, no audio output, no controller. Upload and read serial at
// 115200.
#include <Adafruit_Arcade_Machines.h>
#include <hal/arcade_hal_memory.h>
#include <cart/cart_loader.h>
#include <machines/gb/gameboy_core.h>
#include <machines/nes/nes_core.h>
#include "../nes_test/nes_frame_crc.h"

static bool g_ok = false, g_nes = false;
static cart_info_t g_info;
static cart_status_t g_status;
static int g_core_status = 0;
static const char *g_rom_where = "PSRAM";
static size_t g_internal_used = 0; // internal DRAM the core's init took

#ifndef SPIKE_ROM_INTERNAL
#define SPIKE_ROM_INTERNAL 0
#endif
// -DSPIKE_DRAW_EVERY=2: draw only every second frame, as the Feather will
// (it paints ~30 frames a second of 60 emulated). 1 draws every frame.
#ifndef SPIKE_DRAW_EVERY
#define SPIKE_DRAW_EVERY 1
#endif

static bool ends_with(const char *s, const char *ext) {
    const size_t n = strlen(s), m = strlen(ext);
    return n >= m && strcasecmp(s + n - m, ext) == 0;
}

void setup() {
    Serial.begin(115200);
    delay(500);
    const size_t free_bulk = hal_mem_bulk_free();
    const size_t cap = free_bulk > 256u * 1024u ? free_bulk - 256u * 1024u : 0;
    uint8_t *rom = cap ? (uint8_t *)hal_mem_bulk_alloc(cap) : nullptr;
    static const char *const kExts[] = { ".gb", ".nes", nullptr };
    g_status = rom ? cart_load(kExts, rom, (uint32_t)cap, &g_info) : CART_READ_ERROR;
    if (g_status != CART_OK) return;
    g_nes = ends_with(g_info.name, ".nes");

    // -DSPIKE_ROM_INTERNAL=1: copy a ROM of up to 64 KB from PSRAM into
    // internal DRAM, to measure what reading it from PSRAM costs.
#if SPIKE_ROM_INTERNAL
    if (g_info.size <= 64u * 1024u) {
        if (uint8_t *in = (uint8_t *)heap_caps_malloc(g_info.size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)) {
            memcpy(in, rom, g_info.size);
            rom = in;
            g_rom_where = "internal DRAM";
        }
    }
#endif
    const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (g_nes) g_core_status = nes_core_init(rom, g_info.size, 22050);
    else       g_core_status = gameboy_core_init(rom, g_info.size);
    g_internal_used = before - heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    g_ok = (g_core_status == 0);
}

// The host harnesses' scripts: the Game Boy's TEST_AUTOSTART (Tetris), and
// the NES's Start / Right / A taps (Super Mario Bros.).
static uint8_t gb_pad(uint32_t f) {
    uint8_t p = 0;
    if ((f > 900 && f < 910) || (f > 1000 && f < 1010) || (f > 1100 && f < 1110) ||
        (f > 1200 && f < 1210)) p |= GAMEBOY_PAD_START;
    if (f > 1300) p |= ((f / 40) & 1) ? GAMEBOY_PAD_LEFT : GAMEBOY_PAD_RIGHT;
    return p;
}
static uint8_t nes_pad(uint32_t f) {
    uint8_t b = 0;
    if ((f >= 200 && f <= 205) || (f >= 400 && f <= 405) || (f >= 600 && f <= 605)) b |= NES_BTN_START;
    if (f >= 700 && f <= 1500) b |= NES_BTN_RIGHT;
    if ((f >= 800 && f <= 815) || (f >= 900 && f <= 915) || (f >= 1000 && f <= 1015) ||
        (f >= 1100 && f <= 1115)) b |= NES_BTN_A;
    return b;
}

void loop() {
    static uint32_t frame = 0, last = 0, n = 0, sum = 0, worst = 0, aud_worst = 0, worst_all = 0;
    if (!g_ok) {
        if (millis() - last >= 2000) {
            last = millis();
            Serial.printf("[spike] not running: cart status %d (0 ok, 1 no card, 2 no .gb/.nes in /cart, "
                          "3 too big, 4 read error), core status %d\n", (int)g_status, g_core_status);
        }
        return;
    }
    frame++;
    const bool draw = (frame % SPIKE_DRAW_EVERY) == 0;
    uint32_t t0 = micros(), emu, aud;
    if (g_nes) {
        nes_core_set_draw(draw);
        nes_core_set_pad(nes_pad(frame));
        nes_core_frame_begin();
        while (!nes_core_step_line()) {}
        emu = micros() - t0;
        static int16_t samples[NES_APU_MAX_SAMPLES];
        const uint32_t t1 = micros();
        nes_core_audio_render(samples, nes_core_audio_samples_per_frame());
        aud = micros() - t1;
        if (draw) nes_core_swap();
        if (frame == 150 || frame == 650 || frame == 1200 || frame == 1500)
            Serial.printf("[spike] frame %u crc %08X\n", (unsigned)frame,
                          (unsigned)nes_frame_crc(nes_core_front_base()));
    } else {
        gameboy_core_set_draw(draw);
        gameboy_core_set_pad(gb_pad(frame));
        gameboy_core_frame_begin();
        while (!gameboy_core_step(1000)) {}
        emu = micros() - t0;
        static int16_t samples[GAMEBOY_APU_MAX_SAMPLES];
        const uint32_t t1 = micros();
        (void)gameboy_core_audio_frame(samples);
        aud = micros() - t1;
        if (draw) gameboy_core_swap();
    }
    sum += emu; n++;
    if (emu > worst) worst = emu;
    if (aud > aud_worst) aud_worst = aud;
    if (frame > 60 && emu + aud > worst_all) worst_all = emu + aud;
    if (n == 60) {
        Serial.printf("[spike] draw 1 in %d; %s (%u KB in %s; core init took %u B of internal DRAM, %u B left), "
                      "frame %u: emulation mean %uus worst %uus, "
                      "audio worst %uus, emulation+audio worst since start-up %uus (budget 16667us)\n",
                      SPIKE_DRAW_EVERY, g_info.name, (unsigned)(g_info.size / 1024u), g_rom_where,
                      (unsigned)g_internal_used, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)frame,
                      (unsigned)(sum / n), (unsigned)worst, (unsigned)aud_worst, (unsigned)worst_all);
        sum = n = worst = aud_worst = 0;
    }
}
