// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// USB gamepad decoder test: replays reports captured from real controllers
// on the Fruit Jam (examples/SelfTest/usb_gamepad_test_fruitjam) through
// src/input/usb_gamepad_decode and checks the buttons that come out.
//
// Each capture was recorded while pressing one button at a time in a known
// order, so the check is: the distinct non-empty button states, in order,
// must be exactly that order. A capture where the sticks were moved after
// the buttons (the DualShock 4) is checked up to the end of the list only.
//
//   ./build.sh && ./usb_gamepad_test            (all captures; exit 1 on failure)
//   ./usb_gamepad_test --dump captures/X.log    (print every decoded state)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "input/usb_gamepad_decode.h"

namespace {

struct capture_t {
    const char *file;
    std::vector<uint32_t> expect;   // distinct non-empty states, in order
    bool prefix_only;               // more states may follow (stick moves)
};

const std::vector<capture_t> kCaptures = {
    // Pressed: Up Down Left Right, A B X Y, L R, Select Start.
    { "mantapad_snes.log", {
        USB_PAD_UP, USB_PAD_DOWN, USB_PAD_LEFT, USB_PAD_RIGHT,
        USB_PAD_EAST, USB_PAD_SOUTH, USB_PAD_NORTH, USB_PAD_WEST,
        USB_PAD_L1, USB_PAD_R1, USB_PAD_SELECT, USB_PAD_START }, false },
    // Pressed: Up Down Left Right, Cross Circle Square Triangle, L1 R1 L2
    // R2, Share Options, PS, touchpad, L3 R3; then both sticks in circles.
    { "dualshock4.log", {
        USB_PAD_UP, USB_PAD_DOWN, USB_PAD_LEFT, USB_PAD_RIGHT,
        USB_PAD_SOUTH, USB_PAD_EAST, USB_PAD_WEST, USB_PAD_NORTH,
        USB_PAD_L1, USB_PAD_R1, USB_PAD_L2, USB_PAD_R2,
        USB_PAD_SELECT, USB_PAD_START, USB_PAD_HOME, USB_PAD_TOUCH,
        USB_PAD_L3, USB_PAD_R3 }, true },
    // Pressed: Up Down Left Right, A B C X Y Z, Start Mode.
    { "retrobit_genesis.log", {
        USB_PAD_UP, USB_PAD_DOWN, USB_PAD_LEFT, USB_PAD_RIGHT,
        USB_PAD_WEST, USB_PAD_SOUTH, USB_PAD_EAST, USB_PAD_L1, USB_PAD_NORTH, USB_PAD_R1,
        USB_PAD_START, USB_PAD_SELECT }, false },
};

std::vector<uint8_t> hex_bytes(const char *s) {
    std::vector<uint8_t> v;
    for (const char *p = s; *p; ) {
        if (isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1])) {
            v.push_back((uint8_t)strtoul(std::string(p, 2).c_str(), nullptr, 16));
            p += 2;
        } else {
            p++;
        }
    }
    return v;
}

std::string names(uint32_t b) {
    std::string s;
    for (unsigned i = 0; i < USB_PAD_BUTTON_COUNT; i++)
        if (b & (1u << i)) { if (!s.empty()) s += '+'; s += usb_pad_button_name(i); }
    return s.empty() ? "(none)" : s;
}

// Replays one capture; returns the distinct non-empty states it decodes to.
std::vector<uint32_t> replay(const char *path, bool dump, std::string *pad_name) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
    std::vector<uint32_t> states;
    usb_pad_layout_t layout;
    bool have_layout = false, reading_desc = false;
    unsigned vid = 0, pid = 0;
    std::vector<uint8_t> desc;
    uint32_t last = 0;
    char line[2048];

    auto finish_desc = [&]() {
        reading_desc = false;
        const usb_pad_kind_t k = usb_pad_layout_init(&layout, (uint16_t)vid, (uint16_t)pid,
                                                     desc.data(), (uint16_t)desc.size());
        have_layout = k != USB_PAD_UNSUPPORTED;
        *pad_name = layout.name;
        if (dump) printf("  layout: %s, kind %d, %u buttons, hat %d, x %d, y %d\n",
                         layout.name, (int)k, layout.n_buttons, layout.has_hat,
                         layout.has_x, layout.has_y);
    };

    while (fgets(line, sizeof line, f)) {
        if (reading_desc) {
            if (line[0] == ' ') { auto b = hex_bytes(line); desc.insert(desc.end(), b.begin(), b.end()); continue; }
            finish_desc();
        }
        if (strstr(line, "report descriptor")) {
            if (sscanf(strstr(line, "VID "), "VID %x PID %x", &vid, &pid) != 2) continue;
            desc.clear();
            reading_desc = true;
            last = 0;
            continue;
        }
        const char *r = strstr(line, " report ");
        if (!r || !have_layout) continue;
        const char *colon = strchr(r, ':');
        if (!colon) continue;
        std::vector<uint8_t> rep = hex_bytes(colon + 1);
        uint32_t b = last;
        if (!usb_pad_decode(&layout, rep.data(), (uint16_t)rep.size(), &b)) continue;
        if (b != last) {
            if (dump) printf("  %s\n", names(b).c_str());
            if (b) states.push_back(b);
            last = b;
        }
    }
    if (reading_desc) finish_desc();
    fclose(f);
    return states;
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--dump")) {
        std::string name;
        replay(argv[2], true, &name);
        return 0;
    }
    std::string dir = argv[0];
    dir = dir.substr(0, dir.rfind('/') + 1) + "captures/";
    int failures = 0;
    for (const capture_t &c : kCaptures) {
        std::string name;
        const std::vector<uint32_t> got = replay((dir + c.file).c_str(), false, &name);
        bool ok = got.size() >= c.expect.size() && (c.prefix_only || got.size() == c.expect.size());
        size_t first_bad = 0;
        for (; ok && first_bad < c.expect.size(); first_bad++)
            if (got[first_bad] != c.expect[first_bad]) ok = false;
        printf("%-22s %-28s %s (%zu states%s)\n", c.file, name.c_str(), ok ? "PASS" : "FAIL",
               got.size(), c.prefix_only ? ", the first ones checked" : "");
        if (!ok) {
            failures++;
            for (size_t i = 0; i < c.expect.size() || i < got.size(); i++)
                printf("    %2zu expect %-14s got %s\n", i,
                       i < c.expect.size() ? names(c.expect[i]).c_str() : "-",
                       i < got.size() ? names(got[i]).c_str() : "-");
        }
    }
    return failures ? 1 : 0;
}
