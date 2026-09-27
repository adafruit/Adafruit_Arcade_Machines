// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// A master volume for the Feather ESP32 V2, whose MAX98357A amp has none of
// its own. Applied to the finished mix in the audio task, so it covers any
// game without touching its audio code. The arcade sketches use it; the
// consoles scale their own output instead (console_audio_set_volume()), so
// they leave this at full.
#ifndef HAL_AUDIO_FEATHER_ESP32_H
#define HAL_AUDIO_FEATHER_ESP32_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 0 to 256, linear (256 = full, the default; 128 = half the amplitude,
// -6 dB). Takes effect from the next block (~12 ms).
void feather_audio_set_volume(uint32_t volume);

// Moves the volume `steps` steps of 3 dB up (positive) or down, from the
// step nearest the current volume, within 2..256. Returns the new volume.
// For the controller's X + Up/Down (wii_input_feather_esp32.h).
uint32_t feather_audio_volume_step(int steps);

uint32_t feather_audio_volume(void);

#ifdef __cplusplus
}
#endif

#endif
