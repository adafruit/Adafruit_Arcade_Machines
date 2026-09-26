// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// CRC-32 (IEEE, bitwise: slow but table-free) of nofrendo's visible frame,
// 256x240 palette indices, shared by the host harness and the Fruit Jam
// spike sketch so their frames can be compared exactly.
#ifndef NES_FRAME_CRC_H
#define NES_FRAME_CRC_H

#include <stdint.h>

static inline uint32_t nes_frame_crc(const uint8_t *vidbuf) {
    // nofrendo's layout: rows of 8 + 256 + 8 bytes, the picture at +8.
    uint32_t crc = 0xFFFFFFFFu;
    for (int y = 0; y < 240; y++) {
        const uint8_t *row = vidbuf + y * (8 + 256 + 8) + 8;
        for (int x = 0; x < 256; x++) {
            crc ^= row[x];
            for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

#endif
