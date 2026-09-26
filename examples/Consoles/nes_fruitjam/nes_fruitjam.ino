// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// BEFORE YOU BUILD -- set Tools > Optimize to "Optimize Even More (-O3)".
//
// The core's default, "Small (-Os) (standard)", is too slow for the games in
// this library: the frame overruns the display, the scanline queue starves,
// and the screen goes SOLID RED -- which looks exactly like a missing SD
// card. The sketch.yaml beside this file pins -O3 and arduino-cli reads it;
// the Arduino IDE does not always, so set it by hand.

// nes_fruitjam -- the NES on the Adafruit Fruit Jam, with the SD card as the
// cartridge: put ONE .nes (iNES) ROM in /cart/ on the card, and that is the
// game this board plays at power-up. This sketch is the SAMP composition
// root, the one place that knows both the machine (src/machines/nes) and the
// board (src/boards/fruitjam).
//
// The emulator core is nofrendo, vendored from retro-go under the GPL
// version 2 (src/machines/nes/core/VENDORED.md): a firmware built from this
// sketch includes GPL code, which an arcade or Game Boy build does not.
//
// Controls (the Fruit Jam console layout, extras/CONSOLES_PLAN.md):
//   D-pad   UP A3, DOWN A4, LEFT D8, RIGHT D9
//   A       D10 (SHOOT)        B       A2 (ACTION2)
//   Start   D6  (START1)       Select  A5 (COIN)
//   Button 1 STRETCH toggles aspect correction (the NES's 8:7 pixels; starts
//   off, 1:1), as it does in the arcade games.
//   Button 2 ROTATE cycles the picture's rotation.
//   Button 3 MIRROR cycles nofrendo's six palettes.
//   USB gamepads on the Type-A ports: A, B, Start, Select (the Game Boy's
//   mapping; the Retro-bit Genesis pad's B is A and its A is B).
//
// Boot error screens: RED no card, YELLOW no .nes in /cart, MAGENTA the ROM
// is not an iNES image, uses an unsupported mapper, or is too big.
//
// Core 0: emulation, input, audio generation, scanline rendering.
// Core 1: hal_video_run() -- drives the DVI signal; never returns.
#include <Adafruit_Arcade_Machines.h>
#include <hal/arcade_hal_video.h>
#include <hal/arcade_hal_input.h>
#include <machines/nes/nes_machine.h>
#include <machines/nes/nes_core.h>
#include <console/console_audio.h>
#include <console/console_save.h>
#include <boards/fruitjam/board_config_fruitjam.h>
#if defined(USE_TINYUSB)
// USB gamepads on the Type-A ports (Tools > USB Stack > Adafruit TinyUSB,
// which sketch.yaml selects; with the default stack the game still builds
// and runs on the GPIO buttons alone). <pio_usb.h> is here so the builder
// finds the Pico PIO USB library.
#include <pio_usb.h>
#include <boards/fruitjam/usb_input_fruitjam.h>
#endif

static nes_system      g_system;
static volatile bool   g_video_ready = false;
static bool            g_cart_ok     = false;
static uint16_t        g_error_color = 0;

static void print_cart(void) {
    Serial.print("[nes] cart /cart/");
    Serial.print(g_system.cart_name);
    Serial.print(" (");
    Serial.print(g_system.cart_size);
    Serial.print(" bytes, mapper ");
    Serial.print(g_system.mapper);
    Serial.print(" ");
    Serial.print(g_system.mapper_name);
    Serial.print(", ROM in ");
    Serial.print(g_system.rom_in_sram ? "SRAM" : "PSRAM");
    Serial.print(", ");
    Serial.print(g_system.cart_matches);
    Serial.print(" candidate file(s) in /cart, mounted on attempt ");
    Serial.print(g_system.mount_attempts);
    Serial.print("; palette ");
    Serial.print(nes_core_palette_name(g_system.palette));
    Serial.println(")");
}

void setup() {
    Serial.begin(115200);

    // PicoDVI's 640x480 mode requires the system clock to equal the TMDS bit
    // clock (252 MHz) -- before any other peripheral init. The board's own
    // version retimes the PSRAM that holds the cartridge ROM to match
    // (DEVNOTES #129).
    fruitjam_set_sys_clock_khz(252000);

    nes_init_system(&g_system);
#ifdef TEST_ROTATION
    g_system.rotation = (uint8_t)(TEST_ROTATION);
#endif

    // Storage, cartridge load and audio setup -- blocking, and finished
    // before core 1 is allowed to start the display (see g_video_ready).
    g_cart_ok = nes_load_cart(&g_system, &g_error_color);

#if defined(USE_TINYUSB)
    // After the display is initialised (it claims PIO 0) and the clock is
    // at 252 MHz, before core 1 starts the display.
    fruitjam_usb_input_begin(FRUITJAM_USB_MAP_NES);
#endif
    g_video_ready = true;
}

void loop() {
    if (!g_cart_ok) {
        nes_draw_error_frame(g_error_color);
        // Startup messages are lost on this board (the USB serial port only
        // delivers once the host has it open), so the reason is repeated for
        // as long as the error screen is up.
        static uint32_t n = 0;
        if ((n++ % 120u) == 0) {
            Serial.print("[nes] boot error, ");
            Serial.print(nes_boot_error_text(g_system.boot_error));
            Serial.print(" (mount attempts: ");
            Serial.print(g_system.mount_attempts);
            Serial.println(")");
            if (g_system.cart_name[0]) print_cart();
        }
        return;
    }

#if defined(USE_TINYUSB)
    fruitjam_usb_input_poll(); // USB pads merge into hal_input_read() below
#endif
    bool up     = hal_input_read(HAL_BTN_UP);
    bool down   = hal_input_read(HAL_BTN_DOWN);
    bool left   = hal_input_read(HAL_BTN_LEFT);
    bool right  = hal_input_read(HAL_BTN_RIGHT);
    bool a      = hal_input_read(HAL_BTN_SHOOT);
    bool b      = hal_input_read(HAL_BTN_ACTION2);
    bool start  = hal_input_read(HAL_BTN_START1);
    bool select = hal_input_read(HAL_BTN_COIN);
    bool rotate = hal_input_read(HAL_BTN_ROTATE);
    bool palette_next = hal_input_read(HAL_BTN_MIRROR);
    bool stretch = hal_input_read(HAL_BTN_STRETCH);

#ifdef TEST_AUTOSTART
    // Unattended bring-up, the same script as the host harnesses: Start at
    // frames 200, 400 and 600, then Right with A taps.
    {
        static uint32_t f = 0;
        f++;
        if ((f >= 200 && f <= 205) || (f >= 400 && f <= 405) || (f >= 600 && f <= 605)) start = true;
        if (f >= 700 && f <= 1500) right = true;
        if ((f >= 800 && f <= 815) || (f >= 900 && f <= 915) || (f >= 1000 && f <= 1015) ||
            (f >= 1100 && f <= 1115)) a = true;
    }
#endif

    nes_input_update(&g_system, up, down, left, right, a, b, start, select, rotate, palette_next,
                     stretch);

    // Frame-budget instrument, in the same format as the other sketches.
    // `work` is the real cost: frame minus the time spent blocked waiting
    // for the display (hal_video_take_blocked_us()).
    static uint32_t frame_count = 0;
    static uint32_t work_max = 0, work_sum = 0, work_n = 0, blk_sum = 0;
    uint32_t t0 = micros();
    nes_run_frame(&g_system);
    uint32_t frame_us   = micros() - t0;
    uint32_t blocked_us = hal_video_take_blocked_us();
    uint32_t work_us    = (frame_us > blocked_us) ? (frame_us - blocked_us) : 0;
    if (work_us > work_max) work_max = work_us;
    work_sum += work_us; blk_sum += blocked_us; work_n++;

    static uint8_t palette_shown = 0xFF;
    if (g_system.palette != palette_shown) {
        palette_shown = g_system.palette;
        Serial.print("[nes] palette ");
        Serial.println(nes_core_palette_name(palette_shown));
    }

    if ((++frame_count % 60u) == 0) {
        console_audio_stats_t as;
        console_audio_take_stats(&as);
        Serial.print("[nes] frame ");
        Serial.print(frame_count);
        Serial.print(", frame ");
        Serial.print(frame_us);
        Serial.print("us (work ");
        Serial.print(work_us);
        Serial.print("us, blocked ");
        Serial.print(blocked_us);
        Serial.print("us), work_MEAN ");
        Serial.print(work_n ? work_sum / work_n : 0);
        Serial.print("us, work_max ");
        Serial.print(work_max);
        Serial.print("us, blk_MEAN ");
        Serial.print(work_n ? blk_sum / work_n : 0);
        Serial.print("us, line_max ");
        Serial.print(nes_take_line_us_max());
        Serial.print("us, rot ");
        Serial.print((int)g_system.rotation);
        Serial.print(", palette ");
        Serial.print((int)g_system.palette);
        Serial.print(", stretch ");
        Serial.print((int)g_system.stretch);
#if defined(USE_TINYUSB)
        Serial.print(", usb pads ");
        Serial.print(fruitjam_usb_input_pads());
#endif
        Serial.print(", starve ");
        Serial.print(hal_video_take_starve_count());
        Serial.print(", minq ");
        Serial.print(hal_video_take_min_valid_level());
        Serial.print("/");
        Serial.print(hal_video_scanbuf_count());
        Serial.print(", audio ur ");
        Serial.print(as.underruns);
        Serial.print(" ov ");
        Serial.print(as.overruns);
        Serial.print(" depth ");
        Serial.print(as.depth);
        Serial.print(" prod ");
        Serial.print(as.produced);
        Serial.print(" cons ");
        Serial.print(as.consumed);
        Serial.print(" drop ");
        Serial.print(as.dropped);
        Serial.print(" rep ");
        Serial.print(as.repeated);
        Serial.print(", pad 0x");
        Serial.println(g_system.pad, HEX);
        work_max = work_sum = work_n = blk_sum = 0;
        // Battery saves: every heartbeat while one is being written, else
        // every tenth (console_save.h).
        {
            console_save_stats_t ss;
            console_save_take_stats(&ss);
            if (ss.state != CONSOLE_SAVE_NONE &&
                (ss.state == CONSOLE_SAVE_WRITING || (frame_count % 600u) == 0u)) {
                static const char *const names[] = { "none", "UNAVAILABLE", "ready", "writing" };
                Serial.print("[nes] save ");
                Serial.print(names[ss.state]);
                Serial.print(" ");
                Serial.print(ss.path);
                Serial.print(" (");
                Serial.print(ss.size);
                Serial.print(" bytes, loaded ");
                Serial.print(ss.loaded ? "yes" : "no");
                Serial.print("), saves ");
                Serial.print(ss.saves);
                Serial.print(", last took ");
                Serial.print(ss.last_save_frames);
                Serial.print(" frames, busy waits ");
                Serial.print(ss.busy_waits);
                Serial.print(", errors ");
                Serial.print(ss.errors);
                Serial.print(", step_max ");
                Serial.print(ss.step_us_max);
                Serial.println("us");
            }
        }
        if ((frame_count % 600u) == 60u) print_cart();
    }
}

void setup1() {
    while (!g_video_ready) {
        tight_loop_contents(); // spin until core 0 is ready to feed continuously
    }
    hal_video_run(); // never returns
}

void loop1() {
}
