// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See console_save.h. The state machine is the Game Boy's original save
// path (DEVNOTES #130); a change is noticed by comparing the RAM with a
// shadow copy each frame.
#include "console/console_save.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "arch/arch.h"
#include "hal/arcade_hal_storage.h"
#include "storage/extent_lock.h"

namespace {

uint8_t *g_ram = nullptr;       // the core's save RAM
uint8_t *g_shadow = nullptr;    // what it held last frame (change detection)
// What is on the card, sector for sector (padded to whole sectors): a save
// compares the RAM with it, copies in only the sectors that differ, and
// writes only those (see console_save_frame()). Left alone while a write is
// streaming out, since the board may read it later (the Feather does).
uint8_t *g_snapshot = nullptr;
uint32_t g_snapshot_size = 0;
bool     g_snapshot_unknown = false; // a write failed partway: rewrite all next time

hal_storage_extent_t g_extent;   // the whole .sav
hal_storage_extent_t g_write;    // the part of it this save rewrites
uint32_t g_write_last = 0;       // its last sector, as an index into g_extent
console_save_stats_t g_stats;

enum class Step { Idle, Begin, Sector, End };
Step     g_step = Step::Idle;
uint32_t g_sector = 0;
uint32_t g_frames = 0;
uint32_t g_changes = 0;         // frames in which the RAM changed
uint32_t g_seen_changes = 0;    // g_changes as of the last save's snapshot
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

bool console_save_init(const char *rom_name, uint8_t *ram, uint32_t size) {
    memset(&g_stats, 0, sizeof g_stats);
    g_step = Step::Idle;
    if (!ram || size == 0) { g_stats.state = CONSOLE_SAVE_NONE; return false; }
    g_ram = ram;
    g_stats.size = size;
    make_sav_path(rom_name, g_stats.path, sizeof g_stats.path);

    // Load an existing save. A .sav from a PC emulator can be longer than
    // the RAM; only the RAM itself is read, and the rest is left alone.
    if (hal_file_t *f = hal_storage_open(g_stats.path)) {
        const uint32_t got = hal_storage_read(f, ram, size);
        hal_storage_close(f);
        g_stats.loaded = (got == size);
    }

    // Contiguous and full-size from now on, holding what was just loaded
    // (or the RAM as the core reset it), so every later save is a plain
    // in-place rewrite.
    if (!hal_storage_make_contiguous(g_stats.path, size, ram, &g_extent)) {
        g_stats.state = CONSOLE_SAVE_UNAVAILABLE;
        return false;
    }
    g_snapshot_size = g_extent.sectors * HAL_STORAGE_SECTOR;
    g_shadow = (uint8_t *)malloc(size);
    g_snapshot = (uint8_t *)malloc(g_snapshot_size < size ? size : g_snapshot_size);
    if (!g_shadow || !g_snapshot) { g_stats.state = CONSOLE_SAVE_UNAVAILABLE; return false; }
    memcpy(g_shadow, ram, size);
    // The card now holds `ram` (just loaded from it, or just written to it).
    memset(g_snapshot, 0xFF, g_snapshot_size);
    memcpy(g_snapshot, ram, size);
    g_snapshot_unknown = false;
    g_changes = g_seen_changes = 0;
    g_stats.state = CONSOLE_SAVE_READY;
    return true;
}

void console_save_frame(void) {
    g_frames++;
    if (g_stats.state != CONSOLE_SAVE_READY && g_stats.state != CONSOLE_SAVE_WRITING) return;

    const uint64_t t0 = ARCADE_TIME_US64();
    if (memcmp(g_shadow, g_ram, g_stats.size) != 0) {
        memcpy(g_shadow, g_ram, g_stats.size);
        g_changes++;
        g_last_change_frame = g_frames;
    }

    hal_storage_result_t r = HAL_STORAGE_OK;
    switch (g_step) {
    case Step::Idle:
        // Changed since the last save, and quiet for a second -- and the
        // card free: a settings write takes turns with this one
        // (storage/extent_lock.h).
        if (g_changes != g_seen_changes &&
            g_frames - g_last_change_frame >= CONSOLE_SAVE_QUIET_FRAMES &&
            extent_lock_take(EXTENT_OWNER_SAVE)) {
            // ONLY THE SECTORS THAT CHANGED (DEVNOTES #158). Zelda keeps a
            // working buffer in its battery RAM and rewrites it on every
            // screen change, so it saves every few seconds of play; the
            // rest of the 8 KB, including the save slots themselves, is
            // untouched. Rewriting all 16 sectors each time was wear, and a
            // longer window for a power cut, for nothing.
            uint32_t first = UINT32_MAX, last = 0;
            for (uint32_t s = 0; s < g_extent.sectors; s++) {
                const uint32_t off = s * HAL_STORAGE_SECTOR;
                if (off >= g_stats.size) break;
                const uint32_t len = g_stats.size - off < HAL_STORAGE_SECTOR
                                   ? g_stats.size - off : HAL_STORAGE_SECTOR;
                if (g_snapshot_unknown || memcmp(g_snapshot + off, g_ram + off, len) != 0) {
                    memcpy(g_snapshot + off, g_ram + off, len);
                    if (first == UINT32_MAX) first = s;
                    last = s;
                }
            }
            g_seen_changes = g_changes; // later writes make it dirty again
            if (first == UINT32_MAX) {  // changed and changed back: nothing to write
                extent_lock_give(EXTENT_OWNER_SAVE);
                break;
            }
            // One run, first to last changed sector. Sectors between them
            // that didn't change are rewritten with what they already hold.
            g_write = g_extent;
            g_write.first_sector = g_extent.first_sector + first;
            g_write.sectors = last - first + 1u;
            g_sector = first;
            g_write_last = last;
            g_stats.last_save_sectors = g_write.sectors;
            g_step = Step::Begin;
            g_save_started_frame = g_frames;
            g_stats.state = CONSOLE_SAVE_WRITING;
        }
        break;
    case Step::Begin:
        r = hal_storage_extent_write_begin(&g_write);
        if (r == HAL_STORAGE_OK) g_step = Step::Sector;
        break;
    case Step::Sector:
        r = hal_storage_extent_write_sector(g_snapshot + g_sector * HAL_STORAGE_SECTOR);
        if (r == HAL_STORAGE_OK) {
            g_stats.sectors_written++;
            if (g_sector++ == g_write_last) g_step = Step::End;
        }
        break;
    case Step::End:
        r = hal_storage_extent_write_end();
        if (r == HAL_STORAGE_OK) {
            extent_lock_give(EXTENT_OWNER_SAVE);
            g_snapshot_unknown = false;
            g_step = Step::Idle;
            g_stats.state = CONSOLE_SAVE_READY;
            g_stats.saves++;
            g_stats.last_save_frames = g_frames - g_save_started_frame;
        }
        break;
    }
    if (r == HAL_STORAGE_BUSY) g_stats.busy_waits++;
    if (r == HAL_STORAGE_ERROR) {
        // Give up on this save and try again after the quiet period; a
        // successful later save repairs a partly overwritten file.
        g_stats.errors++;
        extent_lock_give(EXTENT_OWNER_SAVE);
        g_snapshot_unknown = true;  // the card may hold part of it: rewrite all
        g_step = Step::Idle;
        g_stats.state = CONSOLE_SAVE_READY;
        g_seen_changes = g_changes - 1u; // still dirty
        g_last_change_frame = g_frames;
    }
    const uint32_t us = (uint32_t)(ARCADE_TIME_US64() - t0);
    if (us > g_stats.step_us_max) g_stats.step_us_max = us;
}

void console_save_take_stats(console_save_stats_t *out) {
    *out = g_stats;
    g_stats.step_us_max = 0;
}
