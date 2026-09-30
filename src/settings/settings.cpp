// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See settings.h.
#include "settings/settings.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "arch/arch.h"
#include "hal/arcade_hal_storage.h"
#include "storage/extent_lock.h"

const char *const SETTINGS_ROTATION_NAMES[4] = { "0", "90", "180", "270" };

namespace {

struct setting_t {
    const char *name;
    const char *const *choices;   // null for an integer
    uint8_t count;
    int32_t lo, hi, def;
    const char *help;
    int32_t value;
};

setting_t    g_keys[SETTINGS_MAX];
uint32_t g_nkeys = 0;
char     g_title[96] = "";

settings_stats_t g_stats;
hal_storage_extent_t g_extent;

// The sector being written (left alone until it's done: on the Feather the
// paint core reads it later), and the text last written, to skip saves
// that would change nothing.
uint8_t g_sector[SETTINGS_FILE_BYTES];
uint8_t g_written[SETTINGS_FILE_BYTES];

bool     g_dirty = false;
uint64_t g_last_change_us = 0;

enum class Step { Idle, Begin, Sector, End };
Step g_step = Step::Idle;

bool valid(int id) { return id >= 0 && (uint32_t)id < g_nkeys; }

bool in_range(const setting_t &k, int32_t v) { return v >= k.lo && v <= k.hi; }

const char *value_text(const setting_t &k, char *buf, size_t n) {
    if (k.choices) return k.choices[k.value];
    snprintf(buf, n, "%ld", (long)k.value);
    return buf;
}

// The whole file, from the template, padded with spaces to one sector.
void render(uint8_t *out) {
    char *p = (char *)out;
    size_t left = SETTINGS_FILE_BYTES;
    auto put = [&](const char *s) {
        const size_t len = strlen(s);
        const size_t n = len < left ? len : left;
        memcpy(p, s, n);
        p += n; left -= n;
    };
    char line[160];
    snprintf(line, sizeof line, "# %s\n", g_title);
    put(line);
    put("# Rewritten by the game 3 s after you change a setting. Safe to\n"
        "# edit here; delete this file to go back to the defaults.\n");
    for (uint32_t i = 0; i < g_nkeys; i++) {
        const setting_t &k = g_keys[i];
        char num[16], val[24], help[96];
        snprintf(val, sizeof val, "%s", value_text(k, num, sizeof num));
        if (k.help) {
            snprintf(help, sizeof help, "%s", k.help);
        } else {
            help[0] = 0;
            for (uint8_t c = 0; c < k.count; c++) {
                strncat(help, k.choices[c], sizeof help - strlen(help) - 1);
                if (c + 1u < k.count) strncat(help, ", ", sizeof help - strlen(help) - 1);
            }
        }
        snprintf(line, sizeof line, "%-8s = %-11s # %s\n", k.name, val, help);
        put(line);
    }
    // Padding the parser ignores: trailing spaces, then a final newline.
    memset(p, ' ', left);
    if (left) out[SETTINGS_FILE_BYTES - 1] = '\n';
}

bool same_word(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

// One line: "key = value", comments and blanks skipped.
void parse_line(char *line) {
    if (char *hash = strchr(line, '#')) *hash = 0;
    char *s = trim(line);
    if (!*s) return;
    char *eq = strchr(s, '=');
    if (!eq) { g_stats.ignored++; return; }
    *eq = 0;
    const char *key = trim(s);
    const char *val = trim(eq + 1);
    for (uint32_t i = 0; i < g_nkeys; i++) {
        setting_t &k = g_keys[i];
        if (!same_word(key, k.name)) continue;
        if (k.choices) {
            for (uint8_t c = 0; c < k.count; c++)
                if (same_word(val, k.choices[c])) { k.value = c; g_stats.applied++; return; }
        } else if (*val) {
            char *end = nullptr;
            const long v = strtol(val, &end, 10);
            if (end && *end == 0 && in_range(k, (int32_t)v)) {
                k.value = (int32_t)v; g_stats.applied++; return;
            }
        }
        g_stats.ignored++;   // a known key with a value it can't use
        return;
    }
    g_stats.ignored++;       // not a key this sketch has
}

void parse(char *text) {
    char *line = text;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        parse_line(line);
        line = nl ? nl + 1 : nullptr;
    }
}

int add(const char *name, const char *const *choices, uint8_t count,
        int32_t lo, int32_t hi, int32_t def, const char *help) {
    if (g_nkeys >= SETTINGS_MAX) return -1;
    setting_t &k = g_keys[g_nkeys];
    k.name = name; k.choices = choices; k.count = count;
    k.lo = lo; k.hi = hi; k.help = help;
    k.def = k.value = in_range(k, def) ? def : lo;
    return (int)g_nkeys++;
}

} // namespace

int settings_add_choice(const char *key, const char *const *names, uint8_t count,
                        uint8_t def, const char *help) {
    if (!names || count == 0 || count > 16) return -1;
    return add(key, names, count, 0, count - 1, def, help);
}

int settings_add_int(const char *key, int32_t lo, int32_t hi, int32_t def,
                     const char *help) {
    return add(key, nullptr, 0, lo, hi, def, help);
}

bool settings_begin(const char *path, const char *title) {
    memset(&g_stats, 0, sizeof g_stats);
    snprintf(g_stats.path, sizeof g_stats.path, "%s", path);
    snprintf(g_title, sizeof g_title, "%s", title ? title : "Settings");
    g_step = Step::Idle;
    g_dirty = false;

    // Read it: the first sector's worth is parsed, and the rest only
    // counted, so a file edited past 512 bytes can be put back to size.
    uint32_t total = 0;
    bool exists = false;
    if (hal_file_t *f = hal_storage_open(path)) {
        exists = true;
        static char text[SETTINGS_FILE_BYTES + 1];
        total = hal_storage_read(f, text, SETTINGS_FILE_BYTES);
        text[total] = 0;
        uint8_t scratch[64];
        while (uint32_t more = hal_storage_read(f, scratch, sizeof scratch)) total += more;
        hal_storage_close(f);
        g_stats.truncated = total > SETTINGS_FILE_BYTES;
        g_stats.loaded = true;
        parse(text);
    }

    // Exactly one sector, holding the values: hal_storage_make_contiguous()
    // leaves a big-enough file as it is, so one of the wrong size is
    // removed first and made again.
    if (exists && total != SETTINGS_FILE_BYTES) hal_storage_remove(path);
    render(g_written);
    if (!hal_storage_make_contiguous(path, SETTINGS_FILE_BYTES, g_written, &g_extent) ||
        g_extent.sectors != 1u) {
        g_stats.state = SETTINGS_UNAVAILABLE;
        return false;
    }
    // An existing correct-size file was left untouched, so the text on the
    // card may differ from the template (comments, spacing). Saving the
    // template over it the first time a value changes is fine.
    g_stats.state = SETTINGS_READY;
    return true;
}

int32_t settings_get(int id) { return valid(id) ? g_keys[id].value : 0; }

void settings_set(int id, int32_t value) {
    if (!valid(id)) return;
    setting_t &k = g_keys[id];
    if (!in_range(k, value) || k.value == value) return;
    k.value = value;
    g_dirty = true;
    g_last_change_us = ARCADE_TIME_US64();
}

void settings_frame(void) {
    if (g_stats.state != SETTINGS_READY && g_stats.state != SETTINGS_WRITING) return;

    hal_storage_result_t r = HAL_STORAGE_OK;
    switch (g_step) {
    case Step::Idle:
        if (!g_dirty) return;
        if (ARCADE_TIME_US64() - g_last_change_us < (uint64_t)SETTINGS_SAVE_DELAY_MS * 1000u)
            return;
        render(g_sector);
        if (memcmp(g_sector, g_written, SETTINGS_FILE_BYTES) == 0) {
            g_dirty = false;   // changed and changed back: nothing to write
            return;
        }
        // Waits for the card if a battery save is streaming out.
        if (!extent_lock_take(EXTENT_OWNER_SETTINGS)) { g_stats.busy_waits++; return; }
        g_dirty = false;       // a change from here on makes it dirty again
        g_step = Step::Begin;
        g_stats.state = SETTINGS_WRITING;
        return;
    case Step::Begin:
        r = hal_storage_extent_write_begin(&g_extent);
        if (r == HAL_STORAGE_OK) g_step = Step::Sector;
        break;
    case Step::Sector:
        r = hal_storage_extent_write_sector(g_sector);
        if (r == HAL_STORAGE_OK) g_step = Step::End;
        break;
    case Step::End:
        r = hal_storage_extent_write_end();
        if (r == HAL_STORAGE_OK) {
            extent_lock_give(EXTENT_OWNER_SETTINGS);
            memcpy(g_written, g_sector, SETTINGS_FILE_BYTES);
            g_step = Step::Idle;
            g_stats.state = SETTINGS_READY;
            g_stats.saves++;
        }
        break;
    }
    if (r == HAL_STORAGE_BUSY) g_stats.busy_waits++;
    if (r == HAL_STORAGE_ERROR) {
        // Try again after the delay; the next successful save repairs it.
        extent_lock_give(EXTENT_OWNER_SETTINGS);
        g_stats.errors++;
        g_step = Step::Idle;
        g_stats.state = SETTINGS_READY;
        g_dirty = true;
        g_last_change_us = ARCADE_TIME_US64();
    }
}

void settings_console_path(const char *rom_name, const char *board, char *out, size_t n) {
    char stem[64];
    snprintf(stem, sizeof stem, "%s", rom_name);
    if (char *dot = strrchr(stem, '.')) *dot = 0;
    snprintf(out, n, "/cart/%s.%s.cfg", stem, board);
}

void settings_describe(char *out, size_t n) {
    if (!n) return;
    out[0] = 0;
    for (uint32_t i = 0; i < g_nkeys; i++) {
        char num[16], part[48];
        snprintf(part, sizeof part, "%s%s %s", i ? ", " : "", g_keys[i].name,
                 value_text(g_keys[i], num, sizeof num));
        strncat(out, part, n - strlen(out) - 1);
    }
}

void settings_take_stats(settings_stats_t *out) { *out = g_stats; }
