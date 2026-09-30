// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// ONE EXTENT REWRITE AT A TIME. The storage contract allows only one
// hal_storage_extent_write_begin() ... end() in progress
// (hal/arcade_hal_storage.h), and two modules use it: battery saves
// (console/console_save.h) and settings (settings/settings.h). Each takes
// this lock before its begin() and gives it back after its end(), so
// neither starts while the other is streaming out.
//
// Atomic, because on the Feather ESP32 the two can run on different cores:
// battery saves on the emulation core, settings on the paint core.
#ifndef EXTENT_LOCK_H
#define EXTENT_LOCK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EXTENT_OWNER_NONE = 0,
    EXTENT_OWNER_SAVE,       // console_save
    EXTENT_OWNER_SETTINGS,   // settings
} extent_owner_t;

// True if `owner` now holds the lock (including if it already did).
bool extent_lock_take(extent_owner_t owner);

// Releases the lock if `owner` holds it; otherwise does nothing.
void extent_lock_give(extent_owner_t owner);

#ifdef __cplusplus
}
#endif

#endif
