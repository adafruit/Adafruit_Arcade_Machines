// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// SCUMM: LucasArts' v3/v4 adventures (Loom, Monkey Island 1 EGA, Indy 3
// EGA, the free demos) through the vendored fruitjam-scumm engine
// (core/VENDORED.md). This is the glue between the engine's C interface,
// core/backend/fj_core.h, and the ArcadeHAL contracts:
//
//   - the engine's 2 MB arena from bulk memory (PSRAM on the Fruit Jam);
//   - its file requests over hal_storage, with the names matched without
//     regard to case;
//   - its 320x200 picture drawn into a 320x240 RGB565 framebuffer, which a
//     board scans out on its own (scumm_video_scanout()), so a room load
//     that keeps the engine busy for 300 ms never stalls the picture;
//   - its 22,050 Hz stereo, mixed to mono for console/console_audio;
//   - the pad as a mouse, and a USB mouse and keyboard (scumm_mouse_input(),
//     scumm_key_hid()).
//
// THE GAME is a folder of game files plus a marker in /cart, as upstream
// does it: /cart/<name>.scumm holds the folder's path (e.g. "/scumm/loom";
// upstream's "/sd/scumm/loom" works too); an empty marker means
// /cart/<name>/. The alphabetically first marker is the game.
//
// THE SKETCH MUST ALSO INCLUDE scumm_new.h, once: it routes C++ `new` to
// the arena, which can't be done from the library (see that file).
#ifndef SCUMM_MACHINE_H
#define SCUMM_MACHINE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCUMM_ARENA_BYTES (2u * 1024u * 1024u)  // as upstream's emu_scumm.py
#define SCUMM_FB_W 320
#define SCUMM_FB_H 240
#define SCUMM_PICTURE_Y 20                       // 320x200 centred in 240 lines

// Boot error colours, the consoles' convention.
#define SCUMM_COLOR_ERROR_NO_CARD 0xF800u   // red
#define SCUMM_COLOR_ERROR_NO_GAME 0xFFE0u   // yellow: no marker, or no folder
#define SCUMM_COLOR_ERROR_ENGINE  0xF81Fu   // magenta: no game found, no memory

typedef enum {
    SCUMM_BOOT_OK = 0,
    SCUMM_BOOT_NO_CARD,
    SCUMM_BOOT_NO_MARKER,      // no .scumm file in /cart
    SCUMM_BOOT_NO_FOLDER,      // the folder it names has no files
    SCUMM_BOOT_NO_MEMORY,      // no bulk memory for the arena
    SCUMM_BOOT_NO_GAME,        // the engine recognised nothing in the folder
    SCUMM_BOOT_ENGINE,         // the engine failed to start
} scumm_boot_error_t;

const char *scumm_boot_error_text(scumm_boot_error_t e);

// Pad buttons for scumm_input_update(), as the sketch maps them.
enum {
    SCUMM_PAD_UP = 1u << 0, SCUMM_PAD_DOWN = 1u << 1,
    SCUMM_PAD_LEFT = 1u << 2, SCUMM_PAD_RIGHT = 1u << 3,
    SCUMM_PAD_A = 1u << 4,       // left click
    SCUMM_PAD_B = 1u << 5,       // right click
    SCUMM_PAD_C = 1u << 6,       // Escape: skip a cutscene
    SCUMM_PAD_START = 1u << 7,   // space: pause (on release)
    SCUMM_PAD_SELECT = 1u << 8,  // F5: the game's save/load screen (on release)
    SCUMM_PAD_START2 = 1u << 9,  // ".": skip the line of dialogue
    SCUMM_PAD_SAVE = 1u << 10,   // quick save to SCUMM_QUICK_SLOT
    SCUMM_PAD_LOAD = 1u << 11,   // quick load from it
};
#define SCUMM_QUICK_SLOT 1    // listed as "Fruit Jam" in the game's load screen

typedef struct {
    scumm_boot_error_t boot_error;
    char     marker[64];       // the .scumm file picked in /cart
    char     folder[96];       // the game folder
    uint32_t files;            // files found in it
    uint32_t matches;          // .scumm markers in /cart
    uint32_t mount_attempts;
    const char *game;          // the engine's name for it, e.g. "loom (EGA Demo)"
    // Pointer, in game coordinates (0..319, 0..199).
    int      x, y;
    uint32_t held;             // frames a direction has been held
    uint8_t  pointer_speed;    // 0..2, as upstream's pointer_speed setting
    uint32_t pad_prev;
    bool     combo;            // Start and Select went down together
    bool     running;          // false once the engine has stopped
    // A USB mouse: its buttons, and motion not yet a whole game pixel.
    uint8_t  mouse_buttons;
    int32_t  mouse_rem_x, mouse_rem_y;
    uint32_t keys;             // key presses sent, for the status line
} scumm_system;

// Boot, in order: init (sets up the display); load (mounts the card and
// finds the game: sys->marker names the settings file); then start, with
// the settings the engine reads once ("pcspk", "adlib" or "none" music).
void scumm_init(scumm_system *sys);
bool scumm_load(scumm_system *sys, uint16_t *out_error_color);
bool scumm_start(scumm_system *sys, const char *music, bool subtitles,
                 uint16_t *out_error_color);

// The pad, once a frame before scumm_run_frame(): SCUMM_PAD_* bits.
void scumm_input_update(scumm_system *sys, uint32_t pad);

// A mouse, once a frame before scumm_input_update(): its motion since the
// last call, in mouse counts (Y down-positive), and its buttons (bit 0
// left, 1 right). The picture is shown at 2x, so two counts are one game
// pixel. Its clicks join the pad's.
void scumm_mouse_input(scumm_system *sys, int32_t dx, int32_t dy, uint8_t buttons);

// A key pressed on a keyboard, as a HID keyboard usage and the modifiers
// held (USB boot-keyboard bits; 0x22 is either Shift). US layout. Keys the
// engine has no use for are ignored. Up to 8 queue between frames.
void scumm_key_hid(scumm_system *sys, uint8_t usage, uint8_t modifiers);

// One 1/60 s engine frame, its audio, and 240 lines into the display queue
// to pace it (not shown: the board scans the framebuffer out). Returns
// false once the engine has stopped (scumm_engine_error() says why).
bool scumm_run_frame(scumm_system *sys);

// For the board's direct scan-out, at 2x: fills a 640-pixel output line
// (`line` 0..479) from the framebuffer. In RAM; see
// fruitjam_video_set_line_source() for the contract.
void scumm_video_scanout(uint32_t line, uint32_t *dst);

void scumm_draw_error_frame(uint16_t color);

const char *scumm_engine_error(void);

// What the machine is doing, for a board's crash report: the sketch can
// register a hook that records it somewhere a reset leaves alone.
enum {
    SCUMM_PHASE_INPUT = 1, SCUMM_PHASE_ENGINE, SCUMM_PHASE_IO_OPEN,
    SCUMM_PHASE_IO_READ, SCUMM_PHASE_IO_WRITE, SCUMM_PHASE_IO_SEEK,
    SCUMM_PHASE_IO_CLOSE, SCUMM_PHASE_AUDIO, SCUMM_PHASE_LINES,
    SCUMM_PHASE_SKETCH = 16,   // the sketch's own phases start here
};
void scumm_set_phase_hook(void (*hook)(uint32_t phase));
const char *scumm_phase_name(uint32_t phase);

typedef struct {
    uint32_t frame_us_max;     // longest scumm_run_frame() engine call
    uint32_t frame_us_sum, frames;
    uint32_t arena_used, arena_peak;
    uint32_t stack_used, stack_size;
    uint32_t audio_frames;     // stereo frames the engine produced
    uint32_t audio_topup;      // of which catch-up (fj_core_mix_extra)
    uint32_t io_opens, io_open_misses, io_reads, io_seeks, io_writes;
    uint32_t io_us_max;        // longest single file request
} scumm_stats_t;
void scumm_take_stats(scumm_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
