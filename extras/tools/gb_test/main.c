// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Conformance runner for the vendored Game Boy core (src/machines/gb/core:
// Peanut-GB + minigb_apu) -- runs a cartridge ROM headless and reports what
// it did. Like m6502_test, it tests one component with nothing attached: no
// machine layer, no HAL, no board.
//
// WHY THIS EXISTS BEFORE THE MACHINE LAYER. The Game Boy is the first
// console, and its core is third-party. A core bug would present as every
// other class of bug at once -- wrong graphics, a hang, a game that boots
// and then misbehaves -- so certainty about the core comes first, with the
// machine layer (src/machines/gb/*.cpp) built on top of it only afterwards.
//
// WHAT IT CHECKS:
//   * blargg's cpu_instrs: the ROM prints its verdict over the Game Boy's
//     serial port, which this runner captures (gb_init_serial). PASS means
//     the serial text contains "Passed all tests".
//   * dmg-acid2: a pixel-exact PPU test. The runner dumps the frame, and
//     acid2_compare.py checks it against the project's reference image.
//   * any other ROM (Tetris): runs N frames, dumps chosen frames as PPM,
//     optionally records the audio to a WAV, and reports the wall-clock
//     cost per frame. Host timing says nothing about the Fruit Jam, whose
//     flash cache this cannot see -- it only catches gross regressions.
//
// THE TEST ROMS ARE NOT IN THIS REPO. dmg-acid2 is MIT; blargg's tests carry
// no license statement. Fetch them with ./fetch_roms.sh, which writes them to
// roms/ (gitignored). Commercial ROMs such as Tetris come from your own
// cartridge dumps.
//
//   ./build.sh
//   ./gb_test roms/cpu_instrs.gb --serial --frames 4000
//   ./gb_test roms/dmg-acid2.gb  --frames 60 --ppm-at 60 --out out
//   python3 acid2_compare.py out/dmg-acid2_f60.ppm roms/reference-dmg.png
//   ./gb_test /path/to/Tetris.gb --frames 600 --ppm-at 300,600 --wav out/tetris.wav
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Audio: the same configuration the Fruit Jam build will use -- 16-bit
// stereo at the arcade games' 22050 Hz output rate.
#define MINIGB_APU_AUDIO_FORMAT_S16SYS
#define AUDIO_SAMPLE_RATE 22050
#include "minigb_apu.h"
#include "minigb_apu.c.inc"

static struct minigb_apu_ctx g_apu;

// Peanut-GB calls these for every APU register access when ENABLE_SOUND is
// set. It declares no prototypes for them, so they must exist before the
// include.
uint8_t audio_read(uint16_t addr) { return minigb_apu_audio_read(&g_apu, addr); }
void audio_write(uint16_t addr, uint8_t val) { minigb_apu_audio_write(&g_apu, addr, val); }

#define ENABLE_SOUND 1
#define ENABLE_LCD 1
#include "peanut_gb.h"

typedef struct {
    uint8_t *rom;
    size_t   rom_size;
    uint8_t  cart_ram[0x20000]; // largest MBC5 RAM; Tetris uses none
    uint8_t  fb[LCD_HEIGHT][LCD_WIDTH];
    char     serial[8192];
    size_t   serial_len;
} host_ctx;

static uint8_t rom_read(struct gb_s *gb, const uint_fast32_t addr) {
    host_ctx *c = (host_ctx *)gb->direct.priv;
    return addr < c->rom_size ? c->rom[addr] : 0xFF;
}
static uint8_t cart_ram_read(struct gb_s *gb, const uint_fast32_t addr) {
    host_ctx *c = (host_ctx *)gb->direct.priv;
    return addr < sizeof c->cart_ram ? c->cart_ram[addr] : 0xFF;
}
static void cart_ram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val) {
    host_ctx *c = (host_ctx *)gb->direct.priv;
    if (addr < sizeof c->cart_ram) c->cart_ram[addr] = val;
}

// A core error is reported and the run stops: continuing would make every
// later frame dump misleading.
static bool g_core_error = false;
static void gb_error(struct gb_s *gb, const enum gb_error_e err, const uint16_t addr) {
    (void)gb;
    static const char *names[] = { "UNKNOWN", "INVALID_OPCODE", "INVALID_READ", "INVALID_WRITE",
                                   "HALT_FOREVER", "INVALID_MAX" };
    fprintf(stderr, "core error %d (%s) at 0x%04X\n", (int)err,
            (unsigned)err < sizeof names / sizeof names[0] ? names[err] : "?", addr);
    g_core_error = true;
}

// Pixel format: bits 0-1 are the shade (0 = lightest). The palette bits
// above them say which palette the pixel came from, which the four-shade
// DMG output here ignores.
static void lcd_draw_line(struct gb_s *gb, const uint8_t *pixels, const uint_fast8_t line) {
    host_ctx *c = (host_ctx *)gb->direct.priv;
    for (int x = 0; x < LCD_WIDTH; x++) c->fb[line][x] = pixels[x] & 3;
}

static void serial_tx(struct gb_s *gb, const uint8_t tx) {
    host_ctx *c = (host_ctx *)gb->direct.priv;
    if (c->serial_len + 1 < sizeof c->serial) c->serial[c->serial_len++] = (char)tx;
    c->serial[c->serial_len] = 0;
}
static enum gb_serial_rx_ret_e serial_rx(struct gb_s *gb, uint8_t *rx) {
    (void)gb; (void)rx;
    return GB_SERIAL_RX_NO_CONNECTION;
}

// Writes the frame as a 160x144 greyscale PPM, shade 0 = white. These are
// the same four levels dmg-acid2's reference image uses.
static bool write_ppm(const host_ctx *c, const char *path) {
    static const uint8_t level[4] = { 0xFF, 0xAA, 0x55, 0x00 };
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return false; }
    fprintf(f, "P6\n%d %d\n255\n", LCD_WIDTH, LCD_HEIGHT);
    for (int y = 0; y < LCD_HEIGHT; y++)
        for (int x = 0; x < LCD_WIDTH; x++) {
            uint8_t v = level[c->fb[y][x]];
            uint8_t px[3] = { v, v, v };
            fwrite(px, 1, 3, f);
        }
    fclose(f);
    return true;
}

// Minimal 16-bit stereo WAV writer; the header is patched with the real size
// at the end.
static void wav_header(FILE *f, uint32_t data_bytes) {
    uint32_t rate = AUDIO_SAMPLE_RATE, byte_rate = rate * 4, riff = 36 + data_bytes;
    uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    uint32_t fmt_len = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt_len, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&byte_rate, 4, 1, f); fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data_bytes, 4, 1, f);
}

static uint8_t *load_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static bool in_list(const char *list, unsigned frame) {
    if (!list) return false;
    for (const char *p = list; *p; ) {
        if ((unsigned)strtoul(p, NULL, 10) == frame) return true;
        p = strchr(p, ',');
        if (!p) break;
        p++;
    }
    return false;
}

static const char *base_name(const char *path) {
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s ROM [--frames N] [--ppm-at F1,F2,...] [--out DIR] "
                        "[--wav FILE] [--serial]\n", argv[0]);
        return 2;
    }
    const char *rom_path = argv[1], *ppm_at = NULL, *out = ".", *wav_path = NULL;
    unsigned frames = 600;
    bool serial = false;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ppm-at") && i + 1 < argc) ppm_at = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--serial")) serial = true;
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }

    static host_ctx ctx;
    static struct gb_s gb;
    ctx.rom = load_file(rom_path, &ctx.rom_size);
    if (!ctx.rom) return 1;
    memset(ctx.cart_ram, 0xFF, sizeof ctx.cart_ram);

    enum gb_init_error_e ie = gb_init(&gb, rom_read, cart_ram_read, cart_ram_write,
                                      gb_error, &ctx);
    if (ie != GB_INIT_NO_ERROR) {
        // This is exactly the check that decides the magenta "wrong
        // cartridge" screen on hardware.
        fprintf(stderr, "gb_init failed: %s\n",
                ie == GB_INIT_INVALID_CHECKSUM ? "INVALID_CHECKSUM" :
                ie == GB_INIT_CARTRIDGE_UNSUPPORTED ? "CARTRIDGE_UNSUPPORTED" : "?");
        return 1;
    }
    gb_init_lcd(&gb, lcd_draw_line);
    gb_init_serial(&gb, serial_tx, serial_rx);
    minigb_apu_audio_init(&g_apu);

    char title[17] = { 0 };
    for (int i = 0; i < 16 && ctx.rom_size > 0x134u + i; i++) {
        char ch = (char)ctx.rom[0x134 + i];
        title[i] = (ch >= 0x20 && ch < 0x7F) ? ch : 0;
    }
    printf("rom %s: %zu bytes, title \"%s\", cart type 0x%02X\n", base_name(rom_path),
           ctx.rom_size, title, ctx.rom_size > 0x147 ? ctx.rom[0x147] : 0);

    FILE *wav = NULL;
    uint32_t wav_bytes = 0;
    if (wav_path) {
        wav = fopen(wav_path, "wb");
        if (!wav) { perror(wav_path); return 1; }
        wav_header(wav, 0);
    }
    // AUDIO_SAMPLES_TOTAL is computed from floating-point constants, so it is
    // not an integer constant expression in C and cannot size an array.
    // A fixed bound, checked once, instead: 22050 Hz / 59.73 fps is 369.
    static audio_sample_t samples[2 * 1024];
    if (AUDIO_SAMPLES_TOTAL > sizeof samples / sizeof samples[0]) {
        fprintf(stderr, "audio buffer too small for %u samples\n", (unsigned)AUDIO_SAMPLES_TOTAL);
        return 1;
    }

    // The ROM's own name, minus extension, prefixes each dump.
    char stem[256];
    snprintf(stem, sizeof stem, "%s", base_name(rom_path));
    char *dot = strrchr(stem, '.');
    if (dot) *dot = 0;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    unsigned f;
    for (f = 1; f <= frames && !g_core_error; f++) {
        gb_run_frame(&gb);
        minigb_apu_audio_callback(&g_apu, samples);
        if (wav) {
            fwrite(samples, sizeof samples[0], AUDIO_SAMPLES_TOTAL, wav);
            wav_bytes += AUDIO_SAMPLES_TOTAL * sizeof samples[0];
        }
        if (in_list(ppm_at, f)) {
            char path[512];
            snprintf(path, sizeof path, "%s/%s_f%u.ppm", out, stem, f);
            if (write_ppm(&ctx, path)) printf("frame %u -> %s\n", f, path);
        }
        // blargg's ROMs finish by printing a verdict; stop as soon as one
        // arrives rather than running out the frame budget.
        if (serial && (strstr(ctx.serial, "Passed all tests") || strstr(ctx.serial, "Failed")))
            { f++; break; }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    unsigned ran = f - 1;
    double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    printf("ran %u frames in %.1f ms (%.3f ms/frame on this host)\n",
           ran, ms, ran ? ms / ran : 0.0);

    if (wav) {
        fseek(wav, 0, SEEK_SET);
        wav_header(wav, wav_bytes);
        fclose(wav);
        printf("audio -> %s (%.1f s at %d Hz)\n", wav_path,
               wav_bytes / 4.0 / AUDIO_SAMPLE_RATE, AUDIO_SAMPLE_RATE);
    }

    int rc = g_core_error ? 1 : 0;
    if (serial) {
        printf("serial output:\n%s\n", ctx.serial);
        bool pass = strstr(ctx.serial, "Passed all tests") != NULL;
        printf("%s: %s\n", base_name(rom_path), pass ? "PASS" : "FAIL");
        if (!pass) rc = 1;
    }
    free(ctx.rom);
    return rc;
}
