// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// hal_video.h for the Feather ESP32 V2 + 2.4" TFT FeatherWing (ILI9341).
//
// The HAL's scanline contract maps onto an SPI panel almost too neatly: set
// the address window once per frame, then stream 240 rows. There is no
// display list and no framebuffer -- a row is written the moment it is
// submitted, which is also why nothing here needs the 40KB scanline queue
// the DVI backend carries.
//
// PACING. On the Fruit Jam, acquire_scanline() BLOCKS on a DVI queue and
// that is what paces frames. Here the SPI write itself is the pace: the bus
// runs at a measured 3.44 MB/s, so a full 320x240 frame costs ~44.6ms and
// the game runs at ~22fps rather than 60. That is a property of the
// wing's wiring, not a setting -- its SPI pins are not the ESP32's IOMUX
// pins, so the GPIO matrix caps the clock. See board_config's note.
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32_V2)

#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include "hal/arcade_hal_video.h"
#include "board_config_feather_esp32.h"

// Same canvas as the Fruit Jam, which is a genuine coincidence worth
// noticing: the DVI backend halves 640x480, and this panel is natively
// 320x240. Every machine's renderer works unchanged.
const uint32_t HAL_VIDEO_WIDTH  = 320;
const uint32_t HAL_VIDEO_HEIGHT = 240;

static Adafruit_ILI9341 s_tft(FEATHER_TFT_CS, FEATHER_TFT_DC);

// Two line buffers, alternated. Not double-buffering in the DMA sense --
// writePixels() is synchronous here -- but it keeps the caller from writing
// into a buffer the driver is still reading if that ever changes.
static uint16_t s_line[2][320];
static uint8_t  s_idx = 0;
static uint32_t s_y = 0;
static bool     s_in_frame = false;

// Instrumentation the sketches print. There is no producer/consumer queue on
// this board, so there is nothing to starve and nothing to measure; these
// return 0 rather than inventing numbers. hal_video.h explicitly allows it.
uint32_t hal_video_take_blocked_us(void)      { return 0; }
uint32_t hal_video_take_starve_count(void)    { return 0; }
uint32_t hal_video_valid_level(void)          { return 0; }
uint32_t hal_video_take_min_valid_level(void) { return 0; }
uint32_t hal_video_scanbuf_count(void)        { return 2; }

bool hal_video_init(void) {
    // Both other devices on the shared bus must be deselected before the
    // display is talked to, or the STMPE610 drives MISO against the SD card.
    pinMode(FEATHER_SD_CS, OUTPUT);    digitalWrite(FEATHER_SD_CS, HIGH);
    pinMode(FEATHER_STMPE_CS, OUTPUT); digitalWrite(FEATHER_STMPE_CS, HIGH);

    s_tft.begin();
    s_tft.setSPISpeed(40000000);  // 60MHz measures identically; see above
    s_tft.setRotation(1);         // 320 wide x 240 tall
    s_tft.fillScreen(ILI9341_BLACK);
    return true;
}

uint16_t *hal_video_acquire_scanline(void) {
    return s_line[s_idx];
}

void hal_video_submit_scanline(uint16_t *buf) {
    if (!s_in_frame) {
        s_tft.startWrite();
        s_tft.setAddrWindow(0, 0, HAL_VIDEO_WIDTH, HAL_VIDEO_HEIGHT);
        s_in_frame = true;
        s_y = 0;
    }
    s_tft.writePixels(buf, HAL_VIDEO_WIDTH, true);
    s_idx ^= 1;
    if (++s_y >= HAL_VIDEO_HEIGHT) {
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
