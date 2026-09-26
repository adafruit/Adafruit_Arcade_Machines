// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Battery saves for the consoles: a cartridge's battery-backed RAM, kept in
// a standard .sav next to the ROM (/cart/<rom name>.sav), the raw bytes PC
// emulators use. Shared by the Game Boy and the NES; designed for the Game
// Boy (DEVNOTES #130) and taken out of it for the NES (#138, #139).
//
// DECIDED 2026-09-24/25 (extras/CONSOLES_PLAN.md, "The cartridge model"):
// AS THE CARTRIDGE DID IT. A save happens when the game writes its save RAM
// and then leaves it alone for a second -- no save button, no save states.
// The .sav is rewritten IN PLACE, so it stays interchangeable with PC
// emulators; a power cut during the ~0.5 s it takes could leave it half
// old, half new, as a real cartridge's RAM could be, and games guard
// against that themselves (Link's Awakening checksums each of its three
// files).
//
// NOTICING A SAVE: once a frame the RAM is compared with a shadow copy
// (8 KB for Link's Awakening or Zelda, a few microseconds). nofrendo maps
// the NES's cartridge RAM as plain memory, with nothing to count writes;
// the Game Boy used to count them in its core, and moved to this.
//
// NEVER BLOCKING THE DISPLAY. File operations block for 6-30 ms against
// ~2.2 ms of queued picture (examples/SelfTest/sd_write_test_fruitjam), so
// the .sav is made full-size and contiguous at boot and, in the game, only
// ever rewritten one 512-byte sector per frame, and never while the card
// reports busy.
#ifndef CONSOLE_SAVE_H
#define CONSOLE_SAVE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// A save starts once the save RAM has changed and then stayed unchanged for
// this many frames (about a second), so a burst of writes becomes one save.
#define CONSOLE_SAVE_QUIET_FRAMES 60u

typedef enum {
    CONSOLE_SAVE_NONE = 0,     // no battery-backed RAM
    CONSOLE_SAVE_UNAVAILABLE,  // it has some, but the board can't persist it
    CONSOLE_SAVE_READY,        // persisting; idle
    CONSOLE_SAVE_WRITING,      // a save is streaming out
} console_save_state_t;

// BLOCKING, BOOT ONLY, with storage mounted and the core initialised (and,
// for the NES, reset: the reset clears the RAM). Loads /cart/<rom stem>.sav
// into `ram` if it exists, then makes the file full-size and contiguous.
// `ram`/`size` are the cartridge's save RAM; `rom_name` is the ROM's file
// name in /cart. Returns true if storage must stay mounted for later saves.
bool console_save_init(const char *rom_name, uint8_t *ram, uint32_t size);

// Once per frame, in the frame loop, at a point where the display queue has
// been topped up: notices changes, and advances any save by at most one
// step.
void console_save_frame(void);

typedef struct {
    console_save_state_t state;
    bool     loaded;           // an existing .sav was read at boot
    uint32_t size;             // save RAM bytes
    uint32_t saves;            // saves completed since boot
    uint32_t errors;           // storage errors since boot
    uint32_t busy_waits;       // frames a step waited because the card was busy
    uint32_t last_save_frames; // frames the last save took, begin to end
    uint32_t step_us_max;      // worst single step since the last take (then reset)
    char     path[80];
} console_save_stats_t;
void console_save_take_stats(console_save_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
