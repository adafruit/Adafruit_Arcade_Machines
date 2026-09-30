// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// A RAM "card" for the settings host test.
#include "hal/arcade_hal_storage.h"
#include <map>
#include <string>
#include <algorithm>
#include <string.h>
std::map<std::string, std::string> g_card;
struct hal_file { std::string path; size_t pos; };
static std::string g_extent_path, g_pending;
hal_file_t *hal_storage_open(const char *p) {
    if (!g_card.count(p)) return nullptr;
    return new hal_file{p, 0};
}
uint32_t hal_storage_read(hal_file_t *f, void *buf, uint32_t len) {
    const std::string &d = g_card[f->path];
    size_t n = d.size() > f->pos ? std::min<size_t>(len, d.size() - f->pos) : 0;
    memcpy(buf, d.data() + f->pos, n); f->pos += n; return (uint32_t)n;
}
void hal_storage_close(hal_file_t *f) { delete f; }
bool hal_storage_remove(const char *p) { g_card.erase(p); return true; }
bool hal_storage_make_contiguous(const char *p, uint32_t size, const uint8_t *content,
                                 hal_storage_extent_t *out) {
    if (!g_card.count(p) || g_card[p].size() < size)
        g_card[p] = std::string((const char *)content, size);
    out->first_sector = 100; out->sectors = (size + 511) / 512;
    strncpy(out->path, p, sizeof out->path);
    g_extent_path = p;
    return true;
}
hal_storage_result_t hal_storage_extent_write_begin(const hal_storage_extent_t *) {
    g_pending.clear(); return HAL_STORAGE_OK;
}
hal_storage_result_t hal_storage_extent_write_sector(const uint8_t *d) {
    g_pending.append((const char *)d, 512); return HAL_STORAGE_OK;
}
hal_storage_result_t hal_storage_extent_write_end(void) {
    g_card[g_extent_path].replace(0, g_pending.size(), g_pending); return HAL_STORAGE_OK;
}
