// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// pacman_featheresp32 -- Pac-Man on an Adafruit Feather ESP32 V2 with the
// 2.4" TFT FeatherWing. The SAMP composition root for this pair: the only
// place that knows both "this game" and "this board".
//
// ~32fps, not 60, and the reason is arithmetic rather than anything
// fixable in this sketch. The wing's SPI pins are not the ESP32's IOMUX
// pins, so the GPIO matrix caps the bus at 40MHz; a 320x240 RGB565 frame is
// 153,600 bytes, which is 30.7ms of clocking that has to happen no matter
// what the CPU is doing. Measured frame time is 31.4ms -- within 2% of that
// ceiling, because the scanlines go out by DMA and the Z80 emulation now
// runs underneath the transfer instead of after it (see
// src/arch/esp32/arch_spi_dma.h). Emulation is 15.1ms of that 31.4 and is
// essentially free now; the only lever left is the clock.
//
// For scale: the same code on the FIFO path the Arduino core provides ran
// 59.7ms/frame at 16.8fps, because a 64-byte poll loop added 14ms of pure
// overhead AND could not overlap with anything.
//
// Audio is a silent stub on this board for now (see
// hal_audio_feather_esp32.cpp).
//
// Differences from pacman_fruitjam.ino, all forced by the hardware:
//  - no setup1()/loop1(): there is no second-core display pump here, the
//    panel is written from submit_scanline() on this core.
//  - no set_sys_clock_khz(): that is a Pico SDK call. The ESP32 runs at
//    240MHz from the board config.
//  - HAL_BTN_MIRROR and HAL_BTN_STRETCH are not wired and read false.
#include <Adafruit_Arcade_Machines.h>
#include <hal/arcade_hal_video.h>
#include <hal/arcade_hal_input.h>
#include <boards/feather_esp32/board_config_feather_esp32.h>
#include <machines/pacman/pacman_machine.h>
#include <machines/pacman/pacman_video.h>
#include <machines/pacman/pacman_input.h>

static pacman_system g_system;
static bool     g_assets_ok = false;
static uint16_t g_error_color = 0;

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("[pacman-esp32] boot: serial up");

    pacman_init(&g_system);
    Serial.println("[pacman-esp32] boot: pacman_init done");

    g_assets_ok = pacman_load_assets(&g_system, &g_error_color);
    Serial.printf("[pacman-esp32] boot: assets %s\n",
                  g_assets_ok ? "loaded OK" : "FAILED");
    if (!g_assets_ok) {
        Serial.printf("[pacman-esp32]   error colour 0x%04X -- red means no SD "
                      "card / would not mount, yellow means mounted but the "
                      "required ROM files were missing\n", g_error_color);
    }
    Serial.printf("[pacman-esp32] heap free %u, largest block %u, PSRAM %u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                  (unsigned)ESP.getPsramSize());
}

void loop() {
    if (!g_assets_ok) {
        static uint32_t last = 0;
        if (millis() - last > 1000) {
            last = millis();
            Serial.println("[pacman-esp32] asset load failed -- halted");
        }
        pacman_draw_error_frame(g_error_color);
        return;
    }

    bool coin   = hal_input_read(HAL_BTN_COIN);
    bool start1 = hal_input_read(HAL_BTN_START1);
    bool start2 = hal_input_read(HAL_BTN_START2);
    bool up     = hal_input_read(HAL_BTN_UP);
    bool down   = hal_input_read(HAL_BTN_DOWN);
    bool left   = hal_input_read(HAL_BTN_LEFT);
    bool right  = hal_input_read(HAL_BTN_RIGHT);
    bool rotate = hal_input_read(HAL_BTN_ROTATE);
    bool mirror = hal_input_read(HAL_BTN_MIRROR);   // always false here

    pacman_input_update(&g_system, coin, start1, start2,
                        up, down, left, right, rotate, mirror);

    // Whole-frame time: emulation and pixel-pushing together, because they
    // are no longer separable. submit_scanline() hands the row to DMA and
    // returns, so the next scanline's Z80 cycles run while it is still on
    // the wire -- the same overlap the Fruit Jam gets from a second core,
    // bought here with a second buffer instead. A number close to 30.7ms
    // means the transfer is the whole cost and the emulator is hidden
    // inside it; a number well above that means something stopped fitting.
    static uint32_t frame = 0, emul_us = 0, push_us = 0, t_prev = 0;
    uint32_t t0 = micros();
    pacman_run_frame(&g_system);     // emulation AND scanline submission
    uint32_t total = micros() - t0;

    emul_us += total;
    if (++frame % 30u == 0) {
        uint32_t now = millis();
        float fps = 30000.0f / (float)(now - t_prev);
        t_prev = now;
        Serial.printf("[pacman-esp32] frame %lu  %.1f fps  frame %lu us  "
                      "(40MHz wire time for 320x240 is 30,720us)\n",
                      (unsigned long)frame, fps,
                      (unsigned long)(emul_us / 30u));
        emul_us = 0; push_us = 0;
    }
}
