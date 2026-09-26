// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// ArcadeHAL memory contract on the Feather ESP32 V2: bulk memory is its
// 2 MB PSRAM, through ps_malloc. See hal/arcade_hal_memory.h.
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32_V2)

#include <Arduino.h>
#include "hal/arcade_hal_memory.h"

void *hal_mem_bulk_alloc(size_t size) { return ps_malloc(size); }

size_t hal_mem_bulk_free(void) { return (size_t)ESP.getFreePsram(); }

// Internal DRAM if it has room (a plain malloc may already choose PSRAM
// for a large block, so ask for internal memory explicitly), else PSRAM.
void *hal_mem_fast_alloc(size_t size) {
    void *p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p ? p : ps_malloc(size);
}

#endif // ARDUINO_ADAFRUIT_FEATHER_ESP32_V2
