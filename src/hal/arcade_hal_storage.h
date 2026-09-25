// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// ArcadeHAL: storage contract.
//
// A minimal, filesystem-shaped primitive modeled loosely on stdio: mount,
// list a directory, open/read/close a file. A board backend can back this
// with an SD card (as Fruit Jam does), onboard flash, or anything else --
// Machine code (asset manifests, ROM/WAV loading) only ever calls through
// this contract, never touches a filesystem library directly.
#ifndef ARCADE_HAL_STORAGE_H
#define ARCADE_HAL_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_file hal_file_t; // opaque, defined by the board backend

// Mount whatever backing store this board uses. Returns false if no card/
// device is present at all -- callers use this to distinguish "no storage"
// from "storage present but empty/wrong contents" (see
// ArcadeMachine_Invaders's invaders_assets.h for the boot-error-color
// distinction this enables).
bool hal_storage_mount(void);

// Unmount. Call once all loading is done; the reference design never
// touches storage again after boot (everything needed is copied to RAM).
void hal_storage_unmount(void);

// Calls cb(filename, ctx) once per regular file (not subdirectory) found
// directly inside `dir`. Returns false if the directory itself couldn't be
// opened.
typedef void (*hal_storage_dirent_cb)(const char *filename, void *ctx);
bool hal_storage_list_dir(const char *dir, hal_storage_dirent_cb cb, void *ctx);

// Returns NULL on failure (file missing, open error, etc).
hal_file_t *hal_storage_open(const char *path);

// Reads up to `len` bytes into `buf`. Returns the number of bytes actually
// read (0 at EOF or on error).
uint32_t hal_storage_read(hal_file_t *f, void *buf, uint32_t len);

// Closes a file opened by hal_storage_open() or hal_storage_create(). For a
// file being written, this is also when its data and size are committed to
// the card.
void hal_storage_close(hal_file_t *f);

// --- Writing (added for console battery saves) ----------------------------
//
// Nothing in the arcade machines writes, and the reference design never
// touches storage after boot. A console cartridge with a battery-backed save
// does, and so storage stays mounted for it. Writes are SLOW and their
// latency is the card's to decide, not ours: an SD card can stall a write for
// many milliseconds while it manages its own flash. Measure before calling
// these from anywhere near the display loop
// (examples/SelfTest/sd_write_test_fruitjam).

// Opens `path` for writing, creating it, or truncating it if it exists.
// Returns NULL on failure.
hal_file_t *hal_storage_create(const char *path);

// Writes `len` bytes from `buf`. Returns the number actually written; fewer
// than `len` means an error.
uint32_t hal_storage_write(hal_file_t *f, const void *buf, uint32_t len);

// Deletes `path`. Returns true if the file is gone afterwards, including
// when it did not exist to begin with.
bool hal_storage_remove(const char *path);

// Renames `from` to `to`. Fails if `to` already exists -- FAT cannot rename
// over an existing file, and every board behaves the same way -- so a caller
// replacing a file removes the old one first.
bool hal_storage_rename(const char *from, const char *to);

// --- Writing WHILE A GAME RUNS: contiguous files, one sector at a time ------
//
// Everything above blocks, often for 6-30 ms, which the display cannot
// survive mid-game (examples/SelfTest/sd_write_test_fruitjam measured close
// at up to 13 ms and single 512-byte writes at up to 30 ms). What does NOT
// block is writing raw 512-byte sectors of a file that already exists at its
// full size in one contiguous run, polling the card's busy state instead of
// waiting on it. So a save file is prepared once, at boot, and after that
// only ever rewritten in place through the non-blocking calls below.

#define HAL_STORAGE_SECTOR 512u

typedef struct {
    uint32_t first_sector; // the file's first 512-byte sector on the card
    uint32_t sectors;      // how many, contiguous from first_sector
    char     path[96];     // the file, for boards that write through it
} hal_storage_extent_t;

// BLOCKING, BOOT ONLY. Makes `path` a file of at least `size` bytes stored in
// one contiguous run and fills `out`. An existing file that is already big
// enough and contiguous is left exactly as it is. Otherwise the file is
// (re)created at `size` bytes holding `content` -- so pass the data the file
// should hold, which for a save is what was just read from it, if anything.
// Returns false if the board can't do this (see its implementation).
bool hal_storage_make_contiguous(const char *path, uint32_t size,
                                 const uint8_t *content, hal_storage_extent_t *out);

typedef enum {
    HAL_STORAGE_OK = 0,
    HAL_STORAGE_BUSY,   // the card is busy: nothing was done, call again later
    HAL_STORAGE_ERROR,
} hal_storage_result_t;

// NON-BLOCKING. A rewrite of an extent from its first sector: begin(), then
// write_sector() once per sector in order, then end(). Each call either does
// its work -- for write_sector, sending 512 bytes, about 0.35 ms on the Fruit
// Jam -- or returns HAL_STORAGE_BUSY at once, having done nothing. Only one
// rewrite may be in progress, and no other storage call may run until end()
// has returned HAL_STORAGE_OK.
hal_storage_result_t hal_storage_extent_write_begin(const hal_storage_extent_t *e);
hal_storage_result_t hal_storage_extent_write_sector(const uint8_t *data);
hal_storage_result_t hal_storage_extent_write_end(void);

#ifdef __cplusplus
}
#endif

#endif
