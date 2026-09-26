// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// NES machine host harness: runs src/machines/nes the way the sketch does
// -- cartridge loader, the scanline-stepped frame loop and double buffer,
// the canvas renderer with its rotations, the audio ring -- against
// host_common's stdio HAL. See ../README.md.
//
//   ./nes_host --rom carts/smb --frames 1500 --press start@200-205 --ppm-at 650
//   ./nes_host --rom carts/smb --frames 1500 <presses> --crc-at 150,650,1200,1500
//
// --crc-at prints the same frame CRC as the NES spike harness
// (../nes_test, which calls nofrendo's own nes_emulate()); equal CRCs under
// the same input prove the machine's per-scanline loop emulates exactly
// what nes_emulate() does.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "machines/nes/nes_machine.h"
#include "machines/nes/nes_core.h"
#include "machines/nes/nes_video.h"
#include "console/console_audio.h"
#include "host_ppm.h"
#include "../nes_test/nes_frame_crc.h"

extern "C" void host_storage_set_rom_dir(const char *dir);
extern "C" void host_audio_fill(int32_t *out, int count);

namespace {

struct press_t { std::string button; unsigned from, to; };

nes_system g_sys;
uint16_t g_error_color = 0;
bool g_ok = false;

void render(void *ctx, uint32_t y, uint16_t *buf) {
    (void)ctx;
    if (!g_ok) { nes_video_fill_scanline(buf, g_error_color); return; }
    nes_video_render_scanline(y, buf, nes_core_front_base(), nes_core_palette565(g_sys.palette),
                              g_sys.rotation, g_sys.mirror_x, g_sys.stretch);
}

bool held(const std::vector<press_t> &presses, const char *name, unsigned frame) {
    for (const press_t &p : presses)
        if (p.button == name && frame >= p.from && frame <= p.to) return true;
    return false;
}

bool in_list(const char *list, unsigned frame) {
    if (!list) return false;
    for (const char *p = list; *p;) {
        if ((unsigned)strtoul(p, nullptr, 10) == frame) return true;
        p = strchr(p, ',');
        if (!p) break;
        p++;
    }
    return false;
}

void wav_header(FILE *f, uint32_t data_bytes) {
    const uint32_t rate = NES_AUDIO_SAMPLE_RATE, byte_rate = rate * 2, riff = 36 + data_bytes;
    const uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
    const uint32_t fmt_len = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt_len, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&byte_rate, 4, 1, f); fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data_bytes, 4, 1, f);
}

} // namespace

int main(int argc, char **argv) {
    const char *rom_dir = nullptr, *ppm_at = nullptr, *crc_at = nullptr, *out = ".",
               *wav_path = nullptr;
    unsigned frames = 600;
    int rotation = 0, palette = 0;
    bool mirror = false, stretch = false;
    std::vector<press_t> presses;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--rom") && i + 1 < argc) rom_dir = argv[++i];
        else if (!strcmp(a, "--frames") && i + 1 < argc) frames = (unsigned)atoi(argv[++i]);
        else if (!strcmp(a, "--ppm-at") && i + 1 < argc) ppm_at = argv[++i];
        else if (!strcmp(a, "--crc-at") && i + 1 < argc) crc_at = argv[++i];
        else if (!strcmp(a, "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(a, "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(a, "--rotation") && i + 1 < argc) rotation = atoi(argv[++i]);
        else if (!strcmp(a, "--palette") && i + 1 < argc) palette = atoi(argv[++i]);
        else if (!strcmp(a, "--mirror")) mirror = true;
        else if (!strcmp(a, "--stretch")) stretch = true;
        else if (!strcmp(a, "--press") && i + 1 < argc) {
            // BUTTON@FROM-TO: up down left right a b start select
            char name[16]; unsigned from, to;
            if (sscanf(argv[++i], "%15[a-z]@%u-%u", name, &from, &to) != 3) {
                fprintf(stderr, "bad --press %s (want e.g. start@200-205)\n", argv[i]);
                return 2;
            }
            presses.push_back({ name, from, to });
        } else {
            fprintf(stderr, "usage: %s --rom DIR [--frames N] [--ppm-at F1,F2] [--crc-at F1,F2]\n"
                            "       [--out DIR] [--press BUTTON@FROM-TO]... [--rotation 0-3]\n"
                            "       [--mirror] [--stretch] [--palette N] [--wav FILE]\n", argv[0]);
            return 2;
        }
    }
    if (!rom_dir) { fprintf(stderr, "--rom DIR is required (the cartridge's /cart directory)\n"); return 2; }
    host_storage_set_rom_dir(rom_dir);

    nes_init_system(&g_sys);
    g_ok = nes_load_cart(&g_sys, &g_error_color);
    printf("cart: \"%s\" (%u bytes, mapper %d %s, ROM in %s, %u candidate(s))\n",
           g_sys.cart_name, g_sys.cart_size, g_sys.mapper, g_sys.mapper_name,
           g_sys.rom_in_sram ? "SRAM" : "bulk memory", g_sys.cart_matches);
    if (!g_ok) {
        printf("boot error: %s\n", nes_boot_error_text(g_sys.boot_error));
        std::string path = std::string(out) + "/nes_error.ppm";
        if (host_ppm_write(path.c_str(), render, nullptr)) printf("error frame -> %s\n", path.c_str());
        return 1;
    }
    g_sys.rotation = (uint8_t)(rotation & 3);
    g_sys.mirror_x = mirror;
    g_sys.stretch = stretch;
    g_sys.palette = (uint8_t)(palette % (int)nes_core_palette_count());

    FILE *wav = nullptr;
    uint32_t wav_bytes = 0;
    if (wav_path) {
        wav = fopen(wav_path, "wb");
        if (!wav) { perror(wav_path); return 1; }
        wav_header(wav, 0);
    }

    uint32_t underruns = 0, overruns = 0, min_depth = 0xFFFFFFFFu, max_depth = 0;
    double owed = 0.0;
    for (unsigned f = 1; f <= frames; f++) {
        nes_input_update(&g_sys, held(presses, "up", f), held(presses, "down", f),
                         held(presses, "left", f), held(presses, "right", f),
                         held(presses, "a", f), held(presses, "b", f),
                         held(presses, "start", f), held(presses, "select", f), false, false, false);
        nes_run_frame(&g_sys);

        owed += (double)NES_AUDIO_SAMPLE_RATE / 60.0;
        while (owed >= 256.0) {
            int32_t buf[256];
            host_audio_fill(buf, 256);
            owed -= 256.0;
            if (wav) {
                int16_t mono[256];
                for (int i = 0; i < 256; i++) mono[i] = (int16_t)(buf[i] >> 16);
                fwrite(mono, 2, 256, wav);
                wav_bytes += 512;
            }
        }
        console_audio_stats_t as;
        console_audio_take_stats(&as);
        underruns += as.underruns;
        overruns += as.overruns;
        if (f > 60) {
            if (as.depth < min_depth) min_depth = as.depth;
            if (as.depth > max_depth) max_depth = as.depth;
        }

        if (in_list(crc_at, f))
            printf("frame %u crc %08X\n", f, (unsigned)nes_frame_crc(nes_core_front_base()));
        if (in_list(ppm_at, f)) {
            std::string stem = g_sys.cart_name;
            const size_t dot = stem.rfind('.');
            if (dot != std::string::npos) stem.resize(dot);
            char path[512];
            snprintf(path, sizeof path, "%s/%s_r%d%s%s_f%u.ppm", out, stem.c_str(),
                     g_sys.rotation, g_sys.mirror_x ? "m" : "", g_sys.stretch ? "s" : "", f);
            if (host_ppm_write(path, render, nullptr)) printf("frame %u -> %s\n", f, path);
        }
    }
    if (wav) {
        fseek(wav, 0, SEEK_SET);
        wav_header(wav, wav_bytes);
        fclose(wav);
        printf("audio -> %s (%.1f s at %d Hz, drained like the board's ISR)\n", wav_path,
               wav_bytes / 2.0 / NES_AUDIO_SAMPLE_RATE, NES_AUDIO_SAMPLE_RATE);
    }
    printf("ran %u frames; audio underruns %u overruns %u, ring depth after the first second %u..%u\n",
           frames, underruns, overruns, min_depth == 0xFFFFFFFFu ? 0 : min_depth, max_depth);
    return (underruns == 0 && overruns == 0) ? 0 : 1;
}
