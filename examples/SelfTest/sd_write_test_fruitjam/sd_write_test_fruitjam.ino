// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// SD WRITE LATENCY -- how long each storage write operation blocks, on this
// board and this card, under game conditions (252 MHz).
//
// A console battery save is written while the game runs, and the display's
// scanline queue holds only ~2.2 ms of picture, so how long a write can
// block decides how saves must be done (extras/CONSOLES_PLAN.md, the
// cartridge model). This sketch measures it rather than assuming it.
//
// Each cycle does what a save does: create a temporary file, write 8 KB
// (Link's Awakening's save RAM) in 512-byte chunks, close it, remove the
// previous file, rename the new one into place, then read it back and
// verify. It times every step and reports the mean and the worst case over
// 100 cycles, repeatedly, so a serial capture that starts late still gets
// the results. It writes only /sdwtest.tmp and /sdwtest.sav in the card's
// root, and deletes both when it finishes.
//
// No display, no emulator: storage only.
#include <Adafruit_Arcade_Machines.h>
#include <hal/arcade_hal_storage.h>
#include <boards/fruitjam/board_config_fruitjam.h>

#define CYCLES     100
#define SAVE_BYTES 8192
#define CHUNK      512

struct stat_t { uint32_t sum, max, n; };
static void add(stat_t &s, uint32_t us) { s.sum += us; if (us > s.max) s.max = us; s.n++; }

static stat_t st_create, st_chunk, st_close, st_remove, st_rename, st_total;
static uint32_t verify_fail, op_fail;
static bool done = false, mounted = false;
static uint8_t pattern[SAVE_BYTES], readback[SAVE_BYTES];

static void run_cycle(uint32_t c) {
    for (uint32_t i = 0; i < SAVE_BYTES; i++) pattern[i] = (uint8_t)(i * 31u + c);
    const uint32_t t0 = micros();

    uint32_t t = micros();
    hal_file_t *f = hal_storage_create("/sdwtest.tmp");
    add(st_create, micros() - t);
    if (!f) { op_fail++; return; }
    for (uint32_t off = 0; off < SAVE_BYTES; off += CHUNK) {
        t = micros();
        if (hal_storage_write(f, pattern + off, CHUNK) != CHUNK) op_fail++;
        add(st_chunk, micros() - t);
    }
    t = micros(); hal_storage_close(f); add(st_close, micros() - t);
    t = micros(); if (!hal_storage_remove("/sdwtest.sav")) op_fail++; add(st_remove, micros() - t);
    t = micros(); if (!hal_storage_rename("/sdwtest.tmp", "/sdwtest.sav")) op_fail++; add(st_rename, micros() - t);
    add(st_total, micros() - t0);

    hal_file_t *r = hal_storage_open("/sdwtest.sav");
    const uint32_t got = r ? hal_storage_read(r, readback, SAVE_BYTES) : 0;
    if (r) hal_storage_close(r);
    if (got != SAVE_BYTES || memcmp(pattern, readback, SAVE_BYTES) != 0) verify_fail++;
}

static void line(const char *name, const stat_t &s) {
    Serial.printf("  %-24s mean %6lu us   max %7lu us   (%lu samples)\n", name,
                  (unsigned long)(s.n ? s.sum / s.n : 0), (unsigned long)s.max, (unsigned long)s.n);
}

void setup() {
    Serial.begin(115200);
    fruitjam_set_sys_clock_khz(252000); // the games' clock, and PSRAM-safe
    mounted = hal_storage_mount();
}

void loop() {
    static uint32_t cycle = 0;
    if (mounted && !done) {
        run_cycle(cycle++);
        if (cycle == CYCLES) {
            hal_storage_remove("/sdwtest.sav");
            hal_storage_remove("/sdwtest.tmp");
            done = true;
        }
        return;
    }
    Serial.println(mounted ? "[sdwrite] results over 100 save cycles (8 KB each):"
                           : "[sdwrite] SD card did not mount");
    if (mounted) {
        line("create", st_create);
        line("write 512 B", st_chunk);
        line("close (commit)", st_close);
        line("remove old", st_remove);
        line("rename", st_rename);
        line("whole save", st_total);
        Serial.printf("  failed operations %lu, readback mismatches %lu, scratch files deleted\n",
                      (unsigned long)op_fail, (unsigned long)verify_fail);
    }
    delay(3000);
}
