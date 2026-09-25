// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See gameboy_save.h.
#include "gameboy_save.h"

#include <string.h>
#include <stdio.h>

#include "arch/arch.h"
#include "gameboy_core.h"
#include "hal/arcade_hal_storage.h"

namespace {

// The wrapper's save RAM is 32 KB at most (gameboy_core.c), so a snapshot
// fits here. A snapshot, not the live RAM: the game keeps running while the
// save streams out, and could otherwise change bytes mid-write.
uint8_t g_snapshot[0x8000];

hal_storage_extent_t g_extent;
gameboy_save_stats_t g_stats;

enum class Step { Idle, Begin, Sector, End };
Step     g_step = Step::Idle;
uint32_t g_sector = 0;
uint32_t g_frames = 0;           // frame counter
uint32_t g_seen_changes = 0;     // save_changes() as of the last save's snapshot
uint32_t g_last_change_count = 0;
uint32_t g_last_change_frame = 0;
uint32_t g_save_started_frame = 0;

void make_sav_path(const char *rom_name, char *out, size_t n) {
    char stem[64];
    snprintf(stem, sizeof stem, "%s", rom_name);
    char *dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    snprintf(out, n, "/cart/%s.sav", stem);
}

} // namespace

bool gameboy_save_init(const char *rom_name) {
    memset(&g_stats, 0, sizeof g_stats);
    g_step = Step::Idle;
    if (!gameboy_core_has_battery()) { g_stats.state = GAMEBOY_SAVE_NONE; return false; }
    const uint32_t size = gameboy_core_save_size();
    g_stats.size = size;
    if (size == 0) { g_stats.state = GAMEBOY_SAVE_NONE; return false; }
    make_sav_path(rom_name, g_stats.path, sizeof g_stats.path);

    // Load an existing save. A .sav from a PC emulator can be longer than
    // the RAM (MBC3 carts often append real-time-clock state); only the RAM
    // itself is read, and the rest of the file is left alone.
    uint8_t *ram = gameboy_core_save_ram();
    if (hal_file_t *f = hal_storage_open(g_stats.path)) {
        const uint32_t got = hal_storage_read(f, ram, size);
        hal_storage_close(f);
        g_stats.loaded = (got == size);
    }

    // Contiguous and full-size from now on, holding what was just loaded (or
    // the RAM's power-on 0xFF if there was nothing), so every later save is
    // a plain in-place rewrite.
    if (!hal_storage_make_contiguous(g_stats.path, size, ram, &g_extent)) {
        g_stats.state = GAMEBOY_SAVE_UNAVAILABLE;
        return false;
    }
    g_seen_changes = g_last_change_count = gameboy_core_save_changes();
    g_stats.state = GAMEBOY_SAVE_READY;
    return true;
}

void gameboy_save_frame(void) {
    g_frames++;
    if (g_stats.state != GAMEBOY_SAVE_READY && g_stats.state != GAMEBOY_SAVE_WRITING) return;

    const uint32_t changes = gameboy_core_save_changes();
    if (changes != g_last_change_count) {
        g_last_change_count = changes;
        g_last_change_frame = g_frames;
    }

    const uint64_t t0 = ARCADE_TIME_US64();
    hal_storage_result_t r = HAL_STORAGE_OK;
    switch (g_step) {
    case Step::Idle:
        // Changed since the last save, and quiet for a second.
        if (changes != g_seen_changes &&
            g_frames - g_last_change_frame >= GAMEBOY_SAVE_QUIET_FRAMES) {
            memcpy(g_snapshot, gameboy_core_save_ram(), g_stats.size);
            // Pad a short final sector (e.g. MBC2's 512 bytes is exact, but a
            // 2 KB RAM would be too) with the RAM's own fill value.
            const uint32_t padded = g_extent.sectors * HAL_STORAGE_SECTOR;
            if (padded > g_stats.size && padded <= sizeof g_snapshot)
                memset(g_snapshot + g_stats.size, 0xFF, padded - g_stats.size);
            g_seen_changes = changes; // later writes make it dirty again
            g_step = Step::Begin;
            g_save_started_frame = g_frames;
            g_stats.state = GAMEBOY_SAVE_WRITING;
        }
        return;
    case Step::Begin:
        r = hal_storage_extent_write_begin(&g_extent);
        if (r == HAL_STORAGE_OK) { g_step = Step::Sector; g_sector = 0; }
        break;
    case Step::Sector:
        r = hal_storage_extent_write_sector(g_snapshot + g_sector * HAL_STORAGE_SECTOR);
        if (r == HAL_STORAGE_OK && ++g_sector == g_extent.sectors) g_step = Step::End;
        break;
    case Step::End:
        r = hal_storage_extent_write_end();
        if (r == HAL_STORAGE_OK) {
            g_step = Step::Idle;
            g_stats.state = GAMEBOY_SAVE_READY;
            g_stats.saves++;
            g_stats.last_save_frames = g_frames - g_save_started_frame;
        }
        break;
    }
    if (r == HAL_STORAGE_BUSY) g_stats.busy_waits++;
    if (r == HAL_STORAGE_ERROR) {
        // Give up on this save and try again on the next change; the old
        // contents of the file may now be partly overwritten, which a
        // successful later save repairs.
        g_stats.errors++;
        g_step = Step::Idle;
        g_stats.state = GAMEBOY_SAVE_READY;
        g_seen_changes = changes - 1u; // still dirty: retry after the quiet period
        g_last_change_frame = g_frames;
    }
    const uint32_t us = (uint32_t)(ARCADE_TIME_US64() - t0);
    if (us > g_stats.step_us_max) g_stats.step_us_max = us;
}

void gameboy_save_take_stats(gameboy_save_stats_t *out) {
    *out = g_stats;
    g_stats.step_us_max = 0;
}
