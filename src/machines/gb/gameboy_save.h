// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Game Boy battery saves: the cartridge's save RAM, kept in a standard .sav
// file next to the ROM (/cart/<name>.sav), the raw bytes PC emulators use.
//
// DECIDED 2026-09-24/25 (extras/CONSOLES_PLAN.md, "The cartridge model"):
// saves happen only the way the original cartridge did -- when the game
// writes its save RAM -- with no save button and no save states; and the
// .sav is rewritten IN PLACE, so it stays interchangeable with PC emulators.
// A power cut during the ~0.3 s a save takes to write could leave it half
// old, half new, as a real cartridge's RAM could be; games guard against
// that themselves (Link's Awakening checksums each of its three files).
//
// HOW IT AVOIDS BLOCKING THE DISPLAY. Ordinary file writes block for 6-30 ms
// (examples/SelfTest/sd_write_test_fruitjam), against ~2.2 ms of queued
// picture. So the .sav is made full-size and contiguous at boot, and in the
// game it is only ever rewritten sector by sector through the non-blocking
// hal_storage_extent_* calls: at most one sector per frame, and none while
// the card reports busy.
#ifndef GAMEBOY_SAVE_H
#define GAMEBOY_SAVE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// A save starts once save RAM has changed and then stayed unchanged for this
// many frames (about a second), so a burst of writes becomes one save.
#define GAMEBOY_SAVE_QUIET_FRAMES 60u

typedef enum {
    GAMEBOY_SAVE_NONE = 0,     // the cartridge has no battery
    GAMEBOY_SAVE_UNAVAILABLE,  // it has one, but the board can't persist it
    GAMEBOY_SAVE_READY,        // persisting; idle
    GAMEBOY_SAVE_WRITING,      // a save is streaming out
} gameboy_save_state_t;

// BLOCKING, BOOT ONLY, with storage mounted and the core initialised. For a
// battery cartridge: loads /cart/<rom stem>.sav into save RAM if it exists,
// then makes it full-size and contiguous. `rom_name` is the ROM's file name
// in /cart. Returns true if storage must stay mounted for later saves.
bool gameboy_save_init(const char *rom_name);

// Once per frame, in the frame loop, at a point where the video queue has
// been topped up: advances any save by at most one step.
void gameboy_save_frame(void);

typedef struct {
    gameboy_save_state_t state;
    bool     loaded;        // an existing .sav was read at boot
    uint32_t size;          // save RAM bytes
    uint32_t saves;         // saves completed since boot
    uint32_t errors;        // storage errors since boot
    uint32_t busy_waits;    // frames a step waited because the card was busy
    uint32_t last_save_frames; // frames the last save took, begin to end
    uint32_t step_us_max;   // worst single step since the last take (then reset)
    char     path[80];
} gameboy_save_stats_t;
void gameboy_save_take_stats(gameboy_save_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
