// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// NES SPIKE ON THE FRUIT JAM -- how long nofrendo takes to emulate one NES
// frame on the RP2350 at 252 MHz, before anything is ported
// (extras/CONSOLES_PLAN.md, "NES core candidates").
//
// Not a library example: it compiles the nofrendo copy that ../build_spike.sh
// puts in src/nofrendo (fetched, patched, gitignored), so nothing is
// vendored into the library until the core is chosen.
//
// Loads the first .nes in /cart on the SD card, runs it on core 0 with the
// same scripted input as the host harness (Start at frames 200, 400 and
// 600, then Right with A taps), and prints once a second:
//   - nes_emulate() time per frame, mean and worst (the CPU, the PPU
//     drawing all 240 lines into an 8-bit buffer, and the APU making one
//     frame of samples); no display, no audio output, no USB;
//   - at frames 150, 650, 1200 and 1500, a CRC of the frame, to compare with
//       nes_nofrendo --rom SMB.nes --frames 1500 <same presses> --crc-at 150,650,1200,1500
//     (equal CRCs mean the Fruit Jam emulates exactly what the host does).
//
// The ROM is copied into SRAM if it is 256 KB or less, else left in PSRAM;
// -DNES_ROM_IN_PSRAM=1 forces PSRAM, to measure that cost.
#include <Adafruit_Arcade_Machines.h>
#include <hal/arcade_hal_memory.h>
#include <cart/cart_loader.h>
#include <boards/fruitjam/board_config_fruitjam.h>

extern "C" {
#include "src/nofrendo/nofrendo.h"
#include "src/nes_frame_crc.h"
}

#ifndef NES_ROM_IN_PSRAM
#define NES_ROM_IN_PSRAM 0
#endif

static uint8_t g_vidbuf[NES_SCREEN_PITCH * NES_SCREEN_HEIGHT];
static bool g_ok = false;
static const char *g_where = "";
static cart_info_t g_info;
static cart_status_t g_status;

static uint8_t pad_for(uint32_t f) {
    uint8_t b = 0;
    if ((f >= 200 && f <= 205) || (f >= 400 && f <= 405) || (f >= 600 && f <= 605)) b |= NES_PAD_START;
    if (f >= 700 && f <= 1500) b |= NES_PAD_RIGHT;
    if ((f >= 800 && f <= 815) || (f >= 900 && f <= 915) || (f >= 1000 && f <= 1015) ||
        (f >= 1100 && f <= 1115)) b |= NES_PAD_A;
    return b;
}

void setup() {
    Serial.begin(115200);
    fruitjam_set_sys_clock_khz(252000); // the games' clock, PSRAM retimed

    const uint32_t cap = 1024u * 1024u;
    uint8_t *bulk = (uint8_t *)hal_mem_bulk_alloc(cap);
    static const char *const kExts[] = { ".nes", nullptr };
    g_status = bulk ? cart_load(kExts, bulk, cap, &g_info) : CART_READ_ERROR;
    if (g_status != CART_OK) return;

    uint8_t *rom = bulk;
    g_where = "PSRAM";
    if (!NES_ROM_IN_PSRAM && g_info.size <= 256u * 1024u) {
        if (uint8_t *sram = (uint8_t *)malloc(g_info.size)) {
            memcpy(sram, bulk, g_info.size);
            rom = sram;
            g_where = "SRAM";
        }
    }
    if (!nes_init(SYS_NES_NTSC, 22050, false, NULL)) return;
    if (nes_insertcart(rom_loadmem(rom, g_info.size)) != 0) return;
    g_ok = true;
}

void loop() {
    static uint32_t frame = 0, last_report = 0, sum = 0, n = 0, worst = 0, worst_all = 0;

    if (!g_ok) {
        if (millis() - last_report >= 2000) {
            last_report = millis();
            Serial.print("[nes] not running: cart status ");
            Serial.print((int)g_status);
            Serial.print(" (0 ok, 1 no card, 2 no .nes in /cart, 3 too big, 4 read error)");
            Serial.println(g_status == CART_OK ? ", but nofrendo refused the ROM" : "");
        }
        return;
    }

    frame++;
    input_update(0, pad_for(frame));
    nes_setvidbuf(g_vidbuf); // nes_reset() clears it; retro-go sets it every frame
    const uint32_t t0 = micros();
    nes_emulate(true);
    const uint32_t us = micros() - t0;
    sum += us; n++;
    if (us > worst) worst = us;
    if (frame > 60 && us > worst_all) worst_all = us;

    if (frame == 150 || frame == 650 || frame == 1200 || frame == 1500) {
        Serial.print("[nes] frame ");
        Serial.print(frame);
        Serial.print(" crc ");
        Serial.println(nes_frame_crc(g_vidbuf), HEX);
    }
    if (n == 60) {
        Serial.print("[nes] ");
        Serial.print(g_info.name);
        Serial.print(" (");
        Serial.print(g_info.size / 1024u);
        Serial.print(" KB in ");
        Serial.print(g_where);
        Serial.print("), frame ");
        Serial.print(frame);
        Serial.print(": nes_emulate mean ");
        Serial.print(sum / n);
        Serial.print("us, worst ");
        Serial.print(worst);
        Serial.print("us, worst since start-up ");
        Serial.print(worst_all);
        Serial.println("us (budget 16667us)");
        sum = n = worst = 0;
    }
}
