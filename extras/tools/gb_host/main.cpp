// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Host harness for the Game Boy machine (src/machines/gb). Where gb_test
// runs only the vendored core, this runs the whole machine exactly as the
// sketch does -- cartridge loader, core wrapper, the queue-driven frame loop
// and double buffer, the canvas renderer with its rotations, the audio ring
// -- against host_common's stdio HAL. See ../README.md.
//
//   ./build.sh
//   ./gb_host --rom ../gb_test/roms --frames 900 --ppm-at 900 --out out
//   ./gb_host --rom DIR --frames 1500 --press start@900-910 --press start@1000-1010 \
//             --ppm-at 1500 --wav out/play.wav
//
// --rom DIR is the cartridge's /cart directory: the machine lists it and
// takes the first .gb, exactly as it does on the SD card.
//
// AUDIO is drained the way the board drains it -- in 256-sample chunks at
// 22050 samples per second of emulated time -- so the WAV and the counters
// at the end test the ring and its drift correction, not just minigb.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "machines/gb/gameboy_machine.h"
#include "machines/gb/gameboy_core.h"
#include "machines/gb/gameboy_video.h"
#include "machines/gb/gameboy_audio.h"
#include "machines/gb/gameboy_save.h"
#include "host_ppm.h"

extern "C" void host_storage_set_rom_dir(const char *dir);
extern "C" void host_audio_fill(int32_t *out, int count);

namespace {

struct press_t { std::string button; unsigned from, to; };

gameboy_system g_sys;
uint16_t g_error_color = 0;
bool g_ok = false;

void render(void *ctx, uint32_t y, uint16_t *buf) {
    (void)ctx;
    if (!g_ok) { gameboy_video_fill_scanline(buf, g_error_color); return; }
    gameboy_video_render_scanline(y, buf, gameboy_core_front(), g_sys.rotation, g_sys.mirror_x);
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
    const uint32_t rate = GAMEBOY_AUDIO_SAMPLE_RATE, byte_rate = rate * 2, riff = 36 + data_bytes;
    const uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
    const uint32_t fmt_len = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt_len, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&byte_rate, 4, 1, f); fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data_bytes, 4, 1, f);
}

} // namespace

int main(int argc, char **argv) {
    const char *rom_dir = nullptr, *ppm_at = nullptr, *out = ".", *wav_path = nullptr;
    unsigned frames = 600;
    int rotation = 0;
    bool mirror = false;
    std::vector<press_t> presses;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--rom") && i + 1 < argc) rom_dir = argv[++i];
        else if (!strcmp(a, "--frames") && i + 1 < argc) frames = (unsigned)atoi(argv[++i]);
        else if (!strcmp(a, "--ppm-at") && i + 1 < argc) ppm_at = argv[++i];
        else if (!strcmp(a, "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(a, "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(a, "--rotation") && i + 1 < argc) rotation = atoi(argv[++i]);
        else if (!strcmp(a, "--mirror")) mirror = true;
        else if (!strcmp(a, "--press") && i + 1 < argc) {
            // BUTTON@FROM-TO, frames inclusive; buttons are the Game Boy's
            // own names: up down left right a b start select
            char name[16]; unsigned from, to;
            if (sscanf(argv[++i], "%15[a-z]@%u-%u", name, &from, &to) != 3) {
                fprintf(stderr, "bad --press %s (want e.g. start@900-910)\n", argv[i]);
                return 2;
            }
            presses.push_back({ name, from, to });
        } else {
            fprintf(stderr, "usage: %s --rom DIR [--frames N] [--ppm-at F1,F2] [--out DIR]\n"
                            "       [--press BUTTON@FROM-TO]... [--rotation 0-3] [--mirror] [--wav FILE]\n",
                    argv[0]);
            return 2;
        }
    }
    if (!rom_dir) { fprintf(stderr, "--rom DIR is required (the cartridge's /cart directory)\n"); return 2; }
    host_storage_set_rom_dir(rom_dir);

    gameboy_init(&g_sys);
    g_ok = gameboy_load_cart(&g_sys, &g_error_color);
    printf("cart: \"%s\" (%u bytes, title \"%s\", type 0x%02X, %u candidate(s))\n",
           g_sys.cart_name, g_sys.cart_size, g_sys.cart_title, g_sys.cart_type,
           g_sys.cart_matches);
    if (!g_ok) {
        printf("boot error: %s\n", gameboy_boot_error_text(g_sys.boot_error));
        std::string path = std::string(out) + "/gameboy_error.ppm";
        if (host_ppm_write(path.c_str(), render, nullptr)) printf("error frame -> %s\n", path.c_str());
        return 1;
    }
    g_sys.rotation = (uint8_t)(rotation & 3);

    FILE *wav = nullptr;
    uint32_t wav_bytes = 0;
    if (wav_path) {
        wav = fopen(wav_path, "wb");
        if (!wav) { perror(wav_path); return 1; }
        wav_header(wav, 0);
    }

    uint32_t underruns = 0, overruns = 0, min_depth = 0xFFFFFFFFu, max_depth = 0;
    double owed = 0.0; // samples the "ISR" owes, at 22050/s of emulated time
    bool prev_mirror = false;
    for (unsigned f = 1; f <= frames; f++) {
        const bool m = mirror && f == 1; // toggle once, on the first frame
        gameboy_input_update(&g_sys,
                             held(presses, "up", f), held(presses, "down", f),
                             held(presses, "left", f), held(presses, "right", f),
                             held(presses, "a", f), held(presses, "b", f),
                             held(presses, "start", f), held(presses, "select", f),
                             false, m && !prev_mirror);
        prev_mirror = m;
        gameboy_run_frame(&g_sys);

        owed += (double)GAMEBOY_AUDIO_SAMPLE_RATE / 60.0;
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
        gameboy_audio_stats_t as;
        gameboy_audio_take_stats(&as);
        underruns += as.underruns;
        overruns += as.overruns;
        // The first second is the ring settling from its prefill; judge the
        // level only after it.
        if (f > 60) {
            if (as.depth < min_depth) min_depth = as.depth;
            if (as.depth > max_depth) max_depth = as.depth;
        }

        if (in_list(ppm_at, f)) {
            std::string stem = g_sys.cart_name;
            const size_t dot = stem.rfind('.');
            if (dot != std::string::npos) stem.resize(dot);
            char path[512];
            snprintf(path, sizeof path, "%s/%s_r%d%s_f%u.ppm", out, stem.c_str(),
                     g_sys.rotation, g_sys.mirror_x ? "m" : "", f);
            if (host_ppm_write(path, render, nullptr)) printf("frame %u -> %s\n", f, path);
        }
    }
    if (wav) {
        fseek(wav, 0, SEEK_SET);
        wav_header(wav, wav_bytes);
        fclose(wav);
        printf("audio -> %s (%.1f s at %d Hz, drained like the board's ISR)\n", wav_path,
               wav_bytes / 2.0 / GAMEBOY_AUDIO_SAMPLE_RATE, GAMEBOY_AUDIO_SAMPLE_RATE);
    }
    {
        gameboy_save_stats_t ss;
        gameboy_save_take_stats(&ss);
        static const char *const names[] = { "none", "UNAVAILABLE", "ready", "writing" };
        if (ss.state != GAMEBOY_SAVE_NONE)
            printf("save: %s %s (%u bytes, loaded %s), saves %u, last took %u frames, errors %u\n",
                   names[ss.state], ss.path, ss.size, ss.loaded ? "yes" : "no", ss.saves,
                   ss.last_save_frames, ss.errors);
    }
    printf("ran %u frames; core errors %u; audio underruns %u overruns %u, ring depth after the "
           "first second %u..%u\n", frames, gameboy_core_errors(), underruns, overruns,
           min_depth == 0xFFFFFFFFu ? 0 : min_depth, max_depth);
    return (gameboy_core_errors() == 0 && underruns == 0 && overruns == 0) ? 0 : 1;
}
