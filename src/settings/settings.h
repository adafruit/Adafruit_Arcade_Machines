// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Settings saved to the SD card: what a player chose with the buttons
// (rotation, palette, picture size, volume...), kept per game and per
// board in a small text file and applied again at the next power-up.
// extras/CONSOLES_PLAN.md, "Planned: settings saved to the SD card".
//
// THE FILE is plain text, one `key = value` per line, `#` for comments,
// values as words ("90", "fit-smooth", "dmg-green"). Anything it can't
// read -- an unknown key, a bad value, a damaged line -- is ignored and
// that setting keeps its default, so a file never stops a game booting.
// It is always rewritten whole from this module's own template, so values
// edited on a computer are kept but added comments are not.
//
// WRITING WITHOUT STALLING THE DISPLAY, the way battery saves do
// (console/console_save.h): the file is exactly one 512-byte sector, made
// contiguous at boot, and rewritten in place with the non-blocking extent
// calls, SETTINGS_SAVE_DELAY_MS after the last change, only if its text
// changed. It takes turns with battery saves (storage/extent_lock.h).
//
// USE, from a sketch (the composition root, which owns the keys):
//   1. settings_add_choice()/settings_add_int() for each key, with the
//      sketch's defaults;
//   2. settings_begin() at boot, in the SD phase, with storage mounted;
//   3. settings_get() to apply what was read, then any TEST_ build flag;
//   4. each frame, settings_set() with the current values, then
//      settings_frame(). A set that changes nothing costs a compare.
// On the Feather ESP32, call 4 in the paint loop's idle window, where the
// emulation core isn't changing the values.
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_MAX           8u     // keys per file
#define SETTINGS_FILE_BYTES    512u   // one sector (HAL_STORAGE_SECTOR)
#define SETTINGS_SAVE_DELAY_MS 3000u  // after the last change (the user's choice)

// The rotation choices every console and arcade sketch uses: degrees,
// where "90" is the code's rotation 1 (90 degrees counter-clockwise).
extern const char *const SETTINGS_ROTATION_NAMES[4];
// "off", "on", for a setting that is a switch.
extern const char *const SETTINGS_ON_OFF_NAMES[2];

// Registers a key before settings_begin(). A choice is stored as its index
// into `names` (count at most 16); `help` is the comment written after it,
// or null to list the names. Returns the key's id, or -1 if full.
int settings_add_choice(const char *key, const char *const *names, uint8_t count,
                        uint8_t def, const char *help);
// An integer in lo..hi.
int settings_add_int(const char *key, int32_t lo, int32_t hi, int32_t def,
                     const char *help);

// BLOCKING, BOOT ONLY, with storage mounted. Reads `path` (values override
// the defaults), then makes it a contiguous 512-byte file holding the
// values, creating it if missing. `title` is its first comment line; the
// string must last (a literal).
// Returns true if later changes will be saved.
bool settings_begin(const char *path, const char *title);

int32_t settings_get(int id);
// Out-of-range values are ignored. A change is saved SETTINGS_SAVE_DELAY_MS
// later, counting from the last change.
void settings_set(int id, int32_t value);

// Once a frame: starts or advances a save, at most one storage step.
void settings_frame(void);

// "/cart/<rom stem>.<board>.cfg", next to the ROM and its .sav.
void settings_console_path(const char *rom_name, const char *board, char *out, size_t n);
// "/<game>.<board>.cfg", at the root of an arcade game's card.
void settings_arcade_path(const char *game, const char *board, char *out, size_t n);

// "rotation 0, scale fit-smooth, ..." for a serial line.
void settings_describe(char *out, size_t n);
// A whole serial line: "settings <what> <path> (<state>): <values>; read N,
// ignored N, saves N, errors N". `what` is e.g. "loaded" or "saved".
void settings_status_line(const char *what, char *out, size_t n);
// True once each time a save has completed since the last call, for a
// "saved" line.
bool settings_take_saved(void);

typedef enum {
    SETTINGS_NONE = 0,     // settings_begin() not called
    SETTINGS_UNAVAILABLE,  // read what it could, but can't save
    SETTINGS_READY,        // saving; idle
    SETTINGS_WRITING,      // a save is streaming out
} settings_state_t;

typedef struct {
    settings_state_t state;
    bool     loaded;       // a file was read at boot
    uint32_t applied;      // values read from it
    uint32_t ignored;      // lines it couldn't use
    bool     truncated;    // it was longer than 512 bytes
    uint32_t saves;        // saves completed since boot
    uint32_t errors;       // storage errors since boot
    uint32_t busy_waits;   // frames a save waited for the card or a battery save
    char     path[96];
} settings_stats_t;
void settings_take_stats(settings_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
