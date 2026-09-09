// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// hal_video.h for the Feather ESP32 V2 + 2.4" TFT FeatherWing (ILI9341).
//
// The HAL's scanline contract maps onto an SPI panel almost too neatly: set
// the address window once per frame, then stream 240 rows. There is no
// display list and no framebuffer -- a row goes out the moment it is
// submitted, which is also why nothing here needs the 40KB scanline queue
// the DVI backend carries.
//
// PACING. On the Fruit Jam, acquire_scanline() BLOCKS on a DVI queue and
// that is what paces frames. Here the SPI write is the pace: 320x240 RGB565
// is 153,600 bytes, and at the 40MHz this wing's wiring allows that is
// 30.7ms of unavoidable clocking. Nothing in software gets below that
// number; the only lever is how much is added ON TOP of it, and how much of
// it overlaps with emulation.
//
// TWO LAYERS, AND WHICH ONE OWNS WHAT:
//
//   Adafruit_ILI9341   the panel. Init sequence, rotation, address window.
//   src/arch/esp32/    the transport. The chip's SPI DMA engine.
//
// The same division as the audio path, for the same reason -- see
// arch_spi_dma.h. Everything below still goes through the library except
// the pixel bytes themselves.
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32_V2)

#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include "hal/arcade_hal_video.h"
#include "board_config_feather_esp32.h"
#include "arch/esp32/arch_spi_dma.h"

// Same canvas as the Fruit Jam, which is a genuine coincidence worth
// noticing: the DVI backend halves 640x480, and this panel is natively
// 320x240. Every machine's renderer works unchanged.
const uint32_t HAL_VIDEO_WIDTH  = 320;
const uint32_t HAL_VIDEO_HEIGHT = 240;

// The Arduino global `SPI` object is VSPI on the original ESP32 (see
// SPI.cpp), and Adafruit_ILI9341's default constructor uses it. The DMA
// engine has to be pointed at the same host.
#define FEATHER_TFT_SPI_HOST 3

static Adafruit_ILI9341 s_tft(FEATHER_TFT_CS, FEATHER_TFT_DC);

// Two line buffers, alternated -- and here that IS double buffering in the
// DMA sense, not just hygiene. One row is on the wire while the machine
// renders the next into the other. 4-byte alignment is a hard DMA
// requirement and is also what lets the byte swap below work 32 bits at a
// time; static arrays land in internal DRAM, which the DMA engine can
// reach and PSRAM would not be.
static uint16_t s_line[2][320] __attribute__((aligned(4)));
static uint8_t  s_idx = 0;
static uint32_t s_y = 0;
static bool     s_in_frame = false;
static bool     s_dma = false;

// Instrumentation the sketches print. There is no producer/consumer queue on
// this board, so there is nothing to starve and nothing to measure; these
// return 0 rather than inventing numbers. hal_video.h explicitly allows it.
uint32_t hal_video_take_blocked_us(void)      { return 0; }
uint32_t hal_video_take_starve_count(void)    { return 0; }
uint32_t hal_video_valid_level(void)          { return 0; }
uint32_t hal_video_take_min_valid_level(void) { return 0; }
uint32_t hal_video_scanbuf_count(void)        { return 2; }

// RGB565 goes on the wire high byte first. The core's FIFO path did that
// swap for free on its way into the SPI registers (MSB_PIX_SET in
// esp32-hal-spi.c); DMA reads memory verbatim, so it has to happen here.
//
// Two pixels per iteration: 160 loads, shifts and stores for a row, call it
// a microsecond, against a 130us transfer. Doing it in the renderer instead
// would be free-er still, but the renderers are shared with the Fruit Jam
// and its DVI path wants native byte order -- a board's wire format is not
// something a machine should have to know.
static inline void swap_to_wire_order(uint16_t *px, uint32_t n) {
    uint32_t *w = (uint32_t *)px;
    for (uint32_t i = 0; i < n / 2u; i++) {
        uint32_t v = w[i];
        w[i] = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
    }
}

bool hal_video_init(void) {
    // Both other devices on the shared bus must be deselected before the
    // display is talked to, or the STMPE610 drives MISO against the SD card.
    pinMode(FEATHER_SD_CS, OUTPUT);    digitalWrite(FEATHER_SD_CS, HIGH);
    pinMode(FEATHER_STMPE_CS, OUTPUT); digitalWrite(FEATHER_STMPE_CS, HIGH);

    s_tft.begin();
    s_tft.setSPISpeed(40000000);
    s_tft.setRotation(1);         // 320 wide x 240 tall
    s_tft.fillScreen(ILI9341_BLACK);

    // After begin(), so the bus is configured before a channel is bound to
    // it. Failure is not fatal: the FIFO path still works, just slowly, and
    // a board that renders at half speed is far easier to diagnose than one
    // that shows nothing.
    s_dma = arch_spi_dma_init(FEATHER_TFT_SPI_HOST);
    return true;
}

uint16_t *hal_video_acquire_scanline(void) {
    // Never the buffer the DMA engine is reading -- submit_scanline() flips
    // s_idx immediately after handing the other one to the transfer.
    return s_line[s_idx];
}

void hal_video_submit_scanline(uint16_t *buf) {
    if (!s_in_frame) {
        s_tft.startWrite();
        s_tft.setAddrWindow(0, 0, HAL_VIDEO_WIDTH, HAL_VIDEO_HEIGHT);
        s_in_frame = true;
        s_y = 0;
    }

    if (s_dma) {
        // Order matters. The swap touches THIS row, which is not the one in
        // flight, so it runs while the previous transfer is still going --
        // free work. Only then do we wait, and the wait is usually short
        // because the machine spent a scanline's worth of CPU emulation
        // between the two submits.
        swap_to_wire_order(buf, HAL_VIDEO_WIDTH);
        arch_spi_dma_wait();
        arch_spi_dma_write_async(buf, HAL_VIDEO_WIDTH * sizeof(uint16_t));
    } else {
        s_tft.writePixels(buf, HAL_VIDEO_WIDTH, true);
    }

    s_idx ^= 1;
    if (++s_y >= HAL_VIDEO_HEIGHT) {
        // CS must not drop with bytes still queued, and the next thing to
        // touch this bus goes through the CPU-FIFO path.
        if (s_dma) arch_spi_dma_wait();
        s_tft.endWrite();
        s_in_frame = false;
    }
}

// On the Fruit Jam this never returns -- it is the second core's DVI pump.
// Here the panel is driven entirely from submit_scanline() on the calling
// core, so there is no pump to run and the ESP32 sketch simply never calls
// this. Defined anyway so the contract is complete.
void hal_video_run(void) { }

#endif // ARDUINO_ADAFRUIT_FEATHER_ESP32_V2
