// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// NES spike harness (extras/CONSOLES_PLAN.md, "NES core candidates"): one
// front end, one small adapter per core. Each core_*.c fills in a core_t.
#ifndef NES_TEST_HARNESS_H
#define NES_TEST_HARNESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Controller bits, the NES's own order (A B Select Start Up Down Left Right).
enum {
    PAD_A = 0x01, PAD_B = 0x02, PAD_SELECT = 0x04, PAD_START = 0x08,
    PAD_UP = 0x10, PAD_DOWN = 0x20, PAD_LEFT = 0x40, PAD_RIGHT = 0x80,
};

typedef struct {
    const char *name;
    // Load an iNES image (the buffer stays valid for the whole run) and
    // reset. False if the core refuses it.
    bool (*load)(const char *path, uint8_t *rom, size_t size);
    void (*set_pad)(uint8_t bits);
    void (*frame)(void);                 // emulate one video frame
    // A CPU-bus read of cartridge RAM ($6000-$7FFF) with no side effects,
    // for blargg's result protocol.
    uint8_t (*peek_prg_ram)(uint16_t addr);
    // The last frame as 256x240 RGB888.
    void (*rgb)(uint8_t *out);
    // Prints the CPU's state (PC, whether it has jammed); may be NULL.
    void (*debug)(void);
} core_t;

extern const core_t g_core;

#endif
