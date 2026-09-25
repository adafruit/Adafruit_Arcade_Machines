// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See cart_loader.h.
#include "cart_loader.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>

#include "hal/arcade_hal_storage.h"
#include "arch/arch.h"

namespace {

struct scan_ctx {
    const char *const *exts;
    char     best[64];
    bool     have;
    uint32_t matches;
};

bool ends_with_ci(const char *name, const char *ext) {
    const size_t n = strlen(name), e = strlen(ext);
    if (e > n) return false;
    for (size_t i = 0; i < e; i++)
        if (tolower((unsigned char)name[n - e + i]) != tolower((unsigned char)ext[i]))
            return false;
    return true;
}

void on_entry(const char *filename, void *vctx) {
    scan_ctx *c = static_cast<scan_ctx *>(vctx);
    if (filename[0] == '.') return; // AppleDouble sidecars, and anything hidden
    bool ok = false;
    for (const char *const *e = c->exts; *e; e++)
        if (ends_with_ci(filename, *e)) { ok = true; break; }
    if (!ok) return;
    c->matches++;
    // Longer names than the buffer are skipped rather than truncated: a
    // truncated name would open a different file, or none.
    if (strlen(filename) >= sizeof c->best) return;
    if (!c->have || strcmp(filename, c->best) < 0) {
        strcpy(c->best, filename);
        c->have = true;
    }
}

} // namespace

cart_status_t cart_load(const char *const *exts, uint8_t *buf, uint32_t cap,
                        cart_info_t *info) {
    memset(info, 0, sizeof *info);

    // THE MOUNT IS RETRIED, and the attempt that worked is reported. This
    // went in while chasing a RED screen during bring-up, on the theory
    // that this sketch touched the card sooner after boot than the arcade
    // sketches do (they wait 1.5-2 s for a serial monitor). The theory was
    // wrong: all 20 attempts failed, and so did a two-second delay. SdFat's
    // own error, SD_CARD_ERROR_CMD0 with data 0xFF (no response at all, at
    // 12.5 MHz or 1 MHz), pointed at the card, and a different card worked
    // on the first attempt. The retry stays because it costs nothing when
    // the card is fine, and a card that is slow to wake is real. The
    // storage contract has no delay function, so the wait uses the arch
    // clock.
    bool mounted = false;
    for (uint32_t i = 1; i <= CART_MOUNT_TRIES; i++) {
        info->mount_attempts = i;
        if (hal_storage_mount()) { mounted = true; break; }
        const uint64_t until = ARCADE_TIME_US64() + (uint64_t)CART_MOUNT_RETRY_MS * 1000u;
        while (ARCADE_TIME_US64() < until) { }
    }
    if (!mounted) return CART_NO_STORAGE;

    scan_ctx c;
    memset(&c, 0, sizeof c);
    c.exts = exts;
    if (!hal_storage_list_dir(CART_DIR, on_entry, &c)) return CART_NO_ROM;
    info->matches = c.matches;
    if (!c.have) return CART_NO_ROM;
    strcpy(info->name, c.best);

    char path[sizeof CART_DIR + 1 + sizeof c.best];
    snprintf(path, sizeof path, "%s/%s", CART_DIR, c.best);
    hal_file_t *f = hal_storage_open(path);
    if (!f) return CART_READ_ERROR;

    // The storage contract has no size query, so read until end of file,
    // and treat "the buffer filled and there is still more" as too big.
    uint32_t total = 0;
    for (;;) {
        if (total == cap) {
            uint8_t probe;
            const bool more = hal_storage_read(f, &probe, 1) == 1;
            hal_storage_close(f);
            if (more) return CART_TOO_BIG;
            break;
        }
        const uint32_t want = (cap - total) < 4096u ? (cap - total) : 4096u;
        const uint32_t got = hal_storage_read(f, buf + total, want);
        total += got;
        if (got < want) { hal_storage_close(f); break; }
    }
    if (total == 0) return CART_READ_ERROR;
    info->size = total;
    return CART_OK;
}
