// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See usb_input_fruitjam.h.
#include "boards/fruitjam/usb_input_fruitjam.h"

#if defined(ARDUINO_ADAFRUIT_FRUITJAM_RP2350) && defined(USE_TINYUSB)

#include "pico/time.h"
#include "boards/fruitjam/board_config_fruitjam.h"
#include "boards/fruitjam/usb_host_fruitjam.h"
#include "input/usb_gamepad.h"

namespace {

struct entry_t { uint32_t pad; uint8_t btn; };

// Mappings, decided 2026-09-25 (CONSOLES_PLAN.md). Pad buttons are by
// POSITION (usb_gamepad_decode.h), so on Nintendo's layout EAST is A and
// SOUTH is B; these follow the Wii Classic decisions for Nintendo-style
// pads, with the Genesis pad's own choices below.
#define DPAD \
    { USB_PAD_UP, HAL_BTN_UP }, { USB_PAD_DOWN, HAL_BTN_DOWN }, \
    { USB_PAD_LEFT, HAL_BTN_LEFT }, { USB_PAD_RIGHT, HAL_BTN_RIGHT }

// Arcade: A shoots, B is ACTION2, Start is P1, Select is coin, Y is P2.
const entry_t kArcade[] = {
    DPAD,
    { USB_PAD_EAST, HAL_BTN_SHOOT }, { USB_PAD_SOUTH, HAL_BTN_ACTION2 },
    { USB_PAD_START, HAL_BTN_START1 }, { USB_PAD_SELECT, HAL_BTN_COIN },
    { USB_PAD_WEST, HAL_BTN_START2 },
};
// Arcade, Retro-bit Genesis pad: A shoots, X is coin, Start is P1, Y is
// P2. (B keeps ACTION2; C, Z and Mode are unmapped.)
const entry_t kArcadeGenesis[] = {
    DPAD,
    { USB_PAD_WEST, HAL_BTN_SHOOT },   // Genesis A
    { USB_PAD_SOUTH, HAL_BTN_ACTION2 },// Genesis B
    { USB_PAD_L1, HAL_BTN_COIN },      // Genesis X
    { USB_PAD_START, HAL_BTN_START1 },
    { USB_PAD_NORTH, HAL_BTN_START2 }, // Genesis Y
};

// Game Boy (gameboy_fruitjam.ino reads SHOOT as A, ACTION2 as B, START1 as
// Start, COIN as Select): A, B, Start, Select; X, Y, L and R unmapped.
const entry_t kGameBoy[] = {
    DPAD,
    { USB_PAD_EAST, HAL_BTN_SHOOT }, { USB_PAD_SOUTH, HAL_BTN_ACTION2 },
    { USB_PAD_START, HAL_BTN_START1 }, { USB_PAD_SELECT, HAL_BTN_COIN },
};
// Game Boy, Retro-bit Genesis pad: Game Boy A on Genesis B, Game Boy B on
// Genesis A, Start, and Mode as Select.
const entry_t kGameBoyGenesis[] = {
    DPAD,
    { USB_PAD_SOUTH, HAL_BTN_SHOOT },   // Genesis B -> Game Boy A
    { USB_PAD_WEST, HAL_BTN_ACTION2 },  // Genesis A -> Game Boy B
    { USB_PAD_START, HAL_BTN_START1 },
    { USB_PAD_SELECT, HAL_BTN_COIN },   // Genesis Mode -> Select
};

struct table_t { const entry_t *e; uint8_t n; };
#define TABLE(t) { t, (uint8_t)(sizeof t / sizeof t[0]) }

struct pad_override_t { uint16_t vid, pid; table_t arcade, gameboy; };
const pad_override_t kOverrides[] = {
    { 0x0F0D, 0x00C1, TABLE(kArcadeGenesis), TABLE(kGameBoyGenesis) }, // Retro-bit Genesis
};

fruitjam_usb_map_t g_map = FRUITJAM_USB_MAP_ARCADE;
volatile uint32_t g_held = 0;   // bit per HAL_BTN_*
uint8_t g_pads = 0;
bool g_started = false;

table_t table_for(uint16_t vid, uint16_t pid) {
    for (const pad_override_t &o : kOverrides)
        if (o.vid == vid && o.pid == pid) return g_map == FRUITJAM_USB_MAP_GAMEBOY ? o.gameboy : o.arcade;
    return g_map == FRUITJAM_USB_MAP_GAMEBOY ? table_t TABLE(kGameBoy) : table_t TABLE(kArcade);
}

// While core 0 waits for a scanline buffer: run the host, at most once a
// millisecond (an unplug or plug-in is many steps; a report is one).
void idle_hook(void) {
    static uint32_t last = 0;
    const uint32_t now = time_us_32();
    if (now - last < 1000u) return;
    last = now;
    fruitjam_usb_host_task();
}

} // namespace

bool fruitjam_usb_input_begin(fruitjam_usb_map_t map) {
    g_map = map;
    g_started = fruitjam_usb_host_begin();
    if (g_started) fruitjam_video_set_idle_hook(idle_hook);
    return g_started;
}

void fruitjam_usb_input_poll(void) {
    if (!g_started) return;
    fruitjam_usb_host_task();
    uint32_t held = 0;
    uint8_t pads = 0;
    for (uint8_t p = 0; p < USB_GAMEPAD_MAX; p++) {
        usb_gamepad_info_t info;
        if (!usb_gamepad_info(p, &info)) break;
        pads++;
        const uint32_t b = usb_gamepad_buttons(p);
        const table_t t = table_for(info.vid, info.pid);
        for (uint8_t i = 0; i < t.n; i++)
            if (b & t.e[i].pad) held |= 1u << t.e[i].btn;
    }
    g_held = held;
    g_pads = pads;
}

bool fruitjam_usb_input_held(uint8_t hal_btn) {
    return hal_btn < 32 && (g_held & (1u << hal_btn));
}

uint8_t fruitjam_usb_input_pads(void) { return g_pads; }

#endif
