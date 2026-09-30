// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// settings/settings.h against a RAM "card" (fake_storage.cpp): reading a
// hand-edited file, remaking it at 512 bytes, the 3 s save delay, taking
// turns with a battery save, and skipping a save that changes nothing.
// See ../README.md.
#include "settings/settings.h"
#include "storage/extent_lock.h"
#include <map>
#include <string>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
extern std::map<std::string, std::string> g_card;
static const char *const kScale[] = {"1x","fit-nearest","fit-smooth","2x-centre","2x-top","2x-bottom"};
static const char *const kPal[] = {"dmg-green","greys","pocket","gbc"};
int main() {
    const char *path = "/cart/Legend of Zelda, The - Link's Awakening (U) (V1.2) [!].feather.cfg";
    char p2[128]; settings_console_path("Legend of Zelda, The - Link's Awakening (U) (V1.2) [!].gb", "feather", p2, sizeof p2);
    printf("path %s\n", p2);
    assert(std::string(p2) == path);
    g_card[path] = "# mine\nROTATION=90\n  scale = FIT-NEAREST   # x\npalette = purple\nvolume = 300\nturbo = on\nnonsense\n";
    int rot = settings_add_choice("rotation", SETTINGS_ROTATION_NAMES, 4, 0, "0, 90, 180, 270");
    int sc  = settings_add_choice("scale", kScale, 6, 2, nullptr);
    int pal = settings_add_choice("palette", kPal, 4, 0, nullptr);
    int vol = settings_add_int("volume", 0, 256, 16, "0 (mute) to 256");
    bool ok = settings_begin(path, "Game Boy settings for this game on the Feather ESP32 V2.");
    settings_stats_t st; settings_take_stats(&st);
    char d[160]; settings_describe(d, sizeof d);
    printf("begin %d state %d loaded %d applied %u ignored %u | %s\n", ok, st.state, st.loaded, st.applied, st.ignored, d);
    assert(settings_get(rot) == 1 && settings_get(sc) == 1 && settings_get(pal) == 0 && settings_get(vol) == 16);
    assert(st.applied == 2 && st.ignored == 4);
    assert(g_card[path].size() == 512);
    printf("--- file after boot (%zu bytes, text %zu):\n%s|END\n", g_card[path].size(),
           g_card[path].find_last_not_of(" \n") + 1, g_card[path].c_str());
    settings_set(vol, 0);
    for (int i = 0; i < 5; i++) settings_frame();
    assert(g_card[path].find("volume   = 0 ") == std::string::npos);
    usleep(3100 * 1000);
    for (int i = 0; i < 6; i++) settings_frame();
    settings_take_stats(&st);
    assert(st.saves == 1 && g_card[path].find("volume   = 0 ") != std::string::npos);
    extent_lock_take(EXTENT_OWNER_SAVE);
    settings_set(pal, 3); usleep(3100 * 1000);
    for (int i = 0; i < 6; i++) settings_frame();
    settings_take_stats(&st); assert(st.saves == 1 && st.busy_waits > 0);
    extent_lock_give(EXTENT_OWNER_SAVE);
    for (int i = 0; i < 6; i++) settings_frame();
    settings_take_stats(&st); assert(st.saves == 2 && g_card[path].find("palette  = gbc") != std::string::npos);
    settings_set(rot, 2); settings_set(rot, 1); usleep(3100 * 1000);
    for (int i = 0; i < 6; i++) settings_frame();
    settings_take_stats(&st); assert(st.saves == 2);
    // A file of exactly 512 bytes, re-read at the next boot, gives the same values.
    printf("saves %u busy_waits %u\nPASS\n", st.saves, st.busy_waits);
}
