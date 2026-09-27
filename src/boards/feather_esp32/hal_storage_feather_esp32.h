// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// The Feather's save writes, as seen by the rest of its board code
// (hal_storage_feather_esp32.cpp has the design; DEVNOTES #144).
#ifndef HAL_STORAGE_FEATHER_ESP32_H
#define HAL_STORAGE_FEATHER_ESP32_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called by the video HAL at the end of every painted frame, on the
// painting core, once the frame's last transfer has gone out and the bus is
// idle. Carries out the one queued save request, if any and if the card is
// ready. Returns at once when there is nothing to do.
void feather_storage_service(void);

// The longest single service call since the last take, in us (heartbeat).
uint32_t feather_storage_take_service_us_max(void);

#ifdef __cplusplus
}
#endif

#endif
