// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See extent_lock.h.
#include "storage/extent_lock.h"

static volatile uint32_t g_owner = EXTENT_OWNER_NONE;

bool extent_lock_take(extent_owner_t owner) {
    uint32_t expected = EXTENT_OWNER_NONE;
    if (__atomic_compare_exchange_n(&g_owner, &expected, (uint32_t)owner, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return true;
    return expected == (uint32_t)owner;
}

void extent_lock_give(extent_owner_t owner) {
    uint32_t expected = (uint32_t)owner;
    __atomic_compare_exchange_n(&g_owner, &expected, (uint32_t)EXTENT_OWNER_NONE, false,
                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
