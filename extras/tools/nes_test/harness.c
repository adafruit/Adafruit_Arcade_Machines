// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// The shared front end: loads a ROM, runs frames, reads blargg's result
// protocol, writes frames as PPM, and times the emulation.
//
//   nes_nofrendo --rom roms/official_only.nes --blargg
//   nes_fixnes   --rom SMB.nes --frames 1200 --press start@300-305 --ppm-at 600 --time
//
// BLARGG'S PROTOCOL (his test ROMs' readme): $6001-$6003 hold DE B0 61 once
// the test is running; $6000 is 0x80 while running, 0x81 if the test wants
// a reset, and otherwise the result code (0 = passed); $6004 onward is the
// result text, NUL-terminated.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "harness.h"

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static bool in_list(const char *list, unsigned v) {
    if (!list) return false;
    for (const char *p = list; *p; ) {
        if ((unsigned)strtoul(p, (char **)&p, 10) == v) return true;
        if (*p == ',') p++; else break;
    }
    return false;
}

struct press { char name[8]; unsigned from, to; };

static uint8_t pad_for(const struct press *ps, int n, unsigned f) {
    static const struct { const char *n; uint8_t b; } k[] = {
        {"a",PAD_A},{"b",PAD_B},{"select",PAD_SELECT},{"start",PAD_START},
        {"up",PAD_UP},{"down",PAD_DOWN},{"left",PAD_LEFT},{"right",PAD_RIGHT},
    };
    uint8_t bits = 0;
    for (int i = 0; i < n; i++)
        if (f >= ps[i].from && f <= ps[i].to)
            for (unsigned j = 0; j < sizeof k / sizeof k[0]; j++)
                if (!strcmp(ps[i].name, k[j].n)) bits |= k[j].b;
    return bits;
}

static void write_ppm(const char *path) {
    static uint8_t rgb[256 * 240 * 3];
    g_core.rgb(rgb);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n256 240\n255\n");
    fwrite(rgb, 1, sizeof rgb, f);
    fclose(f);
    printf("frame -> %s\n", path);
}

int main(int argc, char **argv) {
    const char *rom_path = NULL, *ppm_at = NULL, *out = ".";
    unsigned frames = 0;
    bool blargg = false, timing = false;
    unsigned trace = 0; // print the CPU state after each of the first N frames
    struct press presses[32]; int n_press = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--rom") && i + 1 < argc) rom_path = argv[++i];
        else if (!strcmp(a, "--frames") && i + 1 < argc) frames = (unsigned)atoi(argv[++i]);
        else if (!strcmp(a, "--ppm-at") && i + 1 < argc) ppm_at = argv[++i];
        else if (!strcmp(a, "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(a, "--blargg")) blargg = true;
        else if (!strcmp(a, "--time")) timing = true;
        else if (!strcmp(a, "--trace") && i + 1 < argc) trace = (unsigned)atoi(argv[++i]);
        else if (!strcmp(a, "--press") && i + 1 < argc && n_press < 32) {
            struct press *p = &presses[n_press];
            if (sscanf(argv[++i], "%7[a-z]@%u-%u", p->name, &p->from, &p->to) == 3) n_press++;
        } else {
            fprintf(stderr, "usage: %s --rom FILE [--frames N] [--blargg] [--time]\n"
                            "       [--press BUTTON@FROM-TO]... [--ppm-at F1,F2] [--out DIR]\n", argv[0]);
            return 2;
        }
    }
    if (!rom_path) { fprintf(stderr, "--rom is required\n"); return 2; }
    if (!frames) frames = blargg ? 3600 : 600;

    FILE *f = fopen(rom_path, "rb");
    if (!f) { perror(rom_path); return 2; }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *rom = malloc((size_t)size);
    if (fread(rom, 1, (size_t)size, f) != (size_t)size) { perror("read"); return 2; }
    fclose(f);
    if (!g_core.load(rom_path, rom, (size_t)size)) {
        printf("%s: %s: the core refused the ROM\n", g_core.name, rom_path);
        return 1;
    }

    const char *base = strrchr(rom_path, '/'); base = base ? base + 1 : rom_path;
    uint64_t t_emu = 0; unsigned timed = 0;
    int result = -1;
    for (unsigned fr = 1; fr <= frames; fr++) {
        g_core.set_pad(pad_for(presses, n_press, fr));
        const uint64_t t0 = now_ns();
        g_core.frame();
        const uint64_t dt = now_ns() - t0;
        if (fr > 60) { t_emu += dt; timed++; } // skip start-up
        if (fr <= trace && g_core.debug) { printf("f%-5u", fr); g_core.debug(); }
        if (in_list(ppm_at, fr)) {
            char p[512];
            snprintf(p, sizeof p, "%s/%s_%s_f%u.ppm", out, g_core.name, base, fr);
            write_ppm(p);
        }
        if (blargg && g_core.peek_prg_ram(0x6001) == 0xDE && g_core.peek_prg_ram(0x6002) == 0xB0 &&
            g_core.peek_prg_ram(0x6003) == 0x61) {
            const uint8_t st = g_core.peek_prg_ram(0x6000);
            if (st < 0x80) { result = st; frames = fr; break; }
        }
    }

    if (blargg && result < 0)
        printf("    (after %u frames: signature %02X %02X %02X, status %02X)\n", frames,
               g_core.peek_prg_ram(0x6001), g_core.peek_prg_ram(0x6002),
               g_core.peek_prg_ram(0x6003), g_core.peek_prg_ram(0x6000));
    if (blargg && result < 0 && g_core.debug) g_core.debug();
    if (blargg) {
        char text[512]; int n = 0;
        for (uint16_t a = 0x6004; a < 0x7000 && n < (int)sizeof text - 1; a++) {
            const uint8_t c = g_core.peek_prg_ram(a);
            if (!c) break;
            text[n++] = (char)c;
        }
        text[n] = 0;
        printf("%-9s %-24s %s (code %d, %u frames)\n", g_core.name, base,
               result == 0 ? "PASS" : result < 0 ? "NO RESULT" : "FAIL", result, frames);
        if (result != 0 && n) printf("    %s\n", text);
    }
    if (timing && timed)
        printf("%-9s %-24s %.1f us per frame over %u frames (host)\n", g_core.name, base,
               (double)t_emu / timed / 1000.0, timed);
    return blargg ? (result == 0 ? 0 : 1) : 0;
}
