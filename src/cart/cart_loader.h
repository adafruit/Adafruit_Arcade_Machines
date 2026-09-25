// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Cartridge loader, shared by every console machine (extras/CONSOLES_PLAN.md,
// "The cartridge model"). The SD card is the cartridge: at power-up the
// console reads whatever ROM is in /cart/, exactly once, before the video
// pump starts -- the same boot-order rule the arcade machines follow.
//
// Board-agnostic: it talks only through hal_storage_*, so it runs unchanged
// in the host harness (extras/tools/gb_host).
#ifndef CART_LOADER_H
#define CART_LOADER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CART_DIR "/cart"
#define CART_MOUNT_TRIES    20
#define CART_MOUNT_RETRY_MS 100

typedef enum {
    CART_OK = 0,
    CART_NO_STORAGE,  // no card, or it won't mount            -> red
    CART_NO_ROM,      // mounted, but no matching file in /cart -> yellow
    CART_TOO_BIG,     // larger than the machine's ROM buffer
    CART_READ_ERROR,  // found, but could not be opened or read
} cart_status_t;

typedef struct {
    char     name[64];  // the file picked, e.g. "Tetris.gb"
    uint32_t size;      // bytes actually loaded
    uint32_t matches;   // how many candidate files /cart held
    uint32_t mount_attempts; // which try mounted the card (1 = first), or
                             // how many failed if it never did
} cart_info_t;

// Finds the cartridge ROM in /cart/ and reads it into `buf`.
//
// The ROM is the alphabetically first regular file whose name ends in one
// of `exts` (case-insensitive; a NULL-terminated list such as
// {".gb", NULL}). Names starting with '.' are skipped: macOS writes an
// AppleDouble sidecar ("._Tetris.gb") next to every file it copies onto a
// FAT card, and it carries the same extension. More than one match is not
// an error; `info->matches` reports it so the sketch can say which it took.
//
// Mounts storage, retrying for up to CART_MOUNT_TRIES x CART_MOUNT_RETRY_MS
// (see cart_loader.cpp for why), and leaves it mounted; the caller unmounts
// once loading is done, as the arcade machines do.
cart_status_t cart_load(const char *const *exts, uint8_t *buf, uint32_t cap,
                        cart_info_t *info);

#ifdef __cplusplus
}
#endif

#endif
