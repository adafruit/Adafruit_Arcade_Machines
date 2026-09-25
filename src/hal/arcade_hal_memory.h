// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// ArcadeHAL: memory contract.
//
// BULK memory is large and slower than on-chip SRAM -- the external PSRAM on
// both the Fruit Jam (8 MB) and the Feather ESP32 V2 (2 MB). It is for data
// too big for SRAM that is still read while a game runs, such as a console
// cartridge ROM. Whether a board has any, and where it lives, is the board's
// business: a machine asks for bulk memory here instead of calling pmalloc()
// or ps_malloc() behind an #ifdef.
//
// Speed caveat, and it matters: on the RP2350, PSRAM is read through the
// same XIP cache as the program in flash, so heavy PSRAM traffic and code
// fetches compete (DEVNOTES #7, #17). Keep the hottest data in SRAM and
// measure what is left in bulk memory. Never touch it from an audio ISR.
//
// (The arcade machines still call ps_malloc() directly on the ESP32; moving
// them onto this contract is a separate cleanup.)
#ifndef ARCADE_HAL_MEMORY_H
#define ARCADE_HAL_MEMORY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Allocates `size` bytes of bulk memory, or returns NULL if the board has
// none or not enough. Allocate once, at boot; there is no need to free.
void *hal_mem_bulk_alloc(size_t size);

// Bytes of bulk memory currently free (0 on a board with none).
size_t hal_mem_bulk_free(void);

#ifdef __cplusplus
}
#endif

#endif
