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

// READBACK PROBE -- set to 1 to enable. Off by default because it paints
// one deliberately noisy scanline across the middle of the picture.
//
// This is the instrument that made the DMA displacement tractable after
// six wrong hypotheses, and it is kept for whoever picks that up again.
// The one rule that matters: CALIBRATE IT AGAINST THE CPU-FIFO PATH
// FIRST. It reports 0/320 there. An instrument not shown to read zero on
// a known-good path proves nothing about a suspect one.
#define FEATHER_TFT_READBACK_PROBE 0
#define PROBE_ROW 120u
static uint16_t s_probe_expect[320];
static bool     s_probe_valid = false;

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

    // The bus still belongs to SPIClass here: asset loading over SdFat has
    // not happened yet. The handover to the IDF driver is in
    // hal_video_run(). See arch_spi_dma.h.
    return true;
}

uint16_t *hal_video_acquire_scanline(void) {
    // Never the buffer the DMA engine is reading -- submit_scanline() flips
    // s_idx immediately after handing the other one to the transfer.
    return s_line[s_idx];
}

// The three ILI9341 address commands, sent through the IDF driver. Once the
// bus is handed over these cannot go through Adafruit_ILI9341, because it
// writes via SPIClass and SPIClass no longer owns the peripheral. The init
// sequence -- the part actually worth a library -- already ran before the
// handover and is untouched.
static void lcd_addr_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    static uint8_t p[4] __attribute__((aligned(4)));
    const uint16_t x2 = (uint16_t)(x + w - 1), y2 = (uint16_t)(y + h - 1);
    arch_spi_lcd_cmd(ILI9341_CASET);
    p[0] = x >> 8; p[1] = (uint8_t)x; p[2] = x2 >> 8; p[3] = (uint8_t)x2;
    arch_spi_lcd_data(p, 4);
    arch_spi_lcd_cmd(ILI9341_PASET);
    p[0] = y >> 8; p[1] = (uint8_t)y; p[2] = y2 >> 8; p[3] = (uint8_t)y2;
    arch_spi_lcd_data(p, 4);
    arch_spi_lcd_cmd(ILI9341_RAMWR);
}

void hal_video_submit_scanline(uint16_t *buf) {
    if (!s_in_frame) {
        if (s_dma) {
            lcd_addr_window(0, 0, HAL_VIDEO_WIDTH, HAL_VIDEO_HEIGHT);
        } else {
            s_tft.startWrite();
            s_tft.setAddrWindow(0, 0, HAL_VIDEO_WIDTH, HAL_VIDEO_HEIGHT);
        }
        s_in_frame = true;
        s_y = 0;
    }

#if FEATHER_TFT_READBACK_PROBE
    if (s_y == PROBE_ROW) {
        uint32_t r = 0x1234567u;
        for (uint32_t x = 0; x < HAL_VIDEO_WIDTH; x++) {
            r = r * 1103515245u + 12345u;
            buf[x] = (uint16_t)(r >> 16);
        }
        memcpy(s_probe_expect, buf, sizeof s_probe_expect);
        s_probe_valid = true;
    }
#endif

    if (s_dma) {
        // The swap touches THIS row, which is not the one in flight, so it
        // runs for free while the previous transfer is still going.
        swap_to_wire_order(buf, HAL_VIDEO_WIDTH);
        arch_spi_lcd_data_async(buf, HAL_VIDEO_WIDTH * sizeof(uint16_t));
    } else {
        s_tft.writePixels(buf, HAL_VIDEO_WIDTH, true);
    }

    s_idx ^= 1;
    if (++s_y >= HAL_VIDEO_HEIGHT) {
        if (s_dma) arch_spi_lcd_flush();
        else       s_tft.endWrite();
        s_in_frame = false;
    }
}

// On the Fruit Jam this never returns -- it is the second core's DVI pump.
// Here it is the SPI BUS HANDOVER, and it returns immediately.
//
// The HAL defines this as "call once the caller is ready to feed scanlines
// continuously", which is exactly the moment the bus can stop belonging to
// SPIClass: the panel is initialised and the ROMs are loaded, and neither
// Adafruit_ILI9341 nor SdFat will touch it again. From here the IDF driver
// owns it. See arch_spi_dma.h.
//
// A sketch that never calls this still works -- it just keeps the slow
// CPU-FIFO path, which is the same fallback used if the handover fails.
void hal_video_run(void) {
    if (s_dma) return;
    s_dma = arch_spi_lcd_begin(FEATHER_TFT_SPI_HOST,
                               SCK, MOSI, MISO,
                               FEATHER_TFT_CS, FEATHER_TFT_DC,
                               40000000);
    Serial.printf("[video] pins sck=%d mosi=%d miso=%d cs=%d dc=%d\n",
                  (int)SCK, (int)MOSI, (int)MISO,
                  (int)FEATHER_TFT_CS, (int)FEATHER_TFT_DC);
    Serial.printf("[video] SPI handover to IDF driver: %s\n",
                  s_dma ? "ok" : "FAILED, staying on the CPU-FIFO path");

    // POST-HANDOVER SELF TEST. Three display-invert flashes, command bytes
    // only, no pixel data. This splits the two ways a working transport can
    // still show nothing: if the panel flashes, chip select and the command
    // path are fine and the fault is in the DATA path (DC level for pixel
    // transfers). If it stays black, the panel is not listening at all and
    // chip select is the problem.
    if (s_dma) {
        for (int i = 0; i < 3; i++) {
            arch_spi_lcd_cmd(ILI9341_INVON);  delay(400);
            arch_spi_lcd_cmd(ILI9341_INVOFF); delay(400);
        }
        Serial.println("[video] invert self-test done (3 flashes if commands land)");
    }
}

// DIAGNOSTIC: read one row back out of the panel and compare it with what
// was sent. Ground truth for what actually arrived, and the only reason
// this artifact was ever pinned down -- it was calibrated against the
// CPU-FIFO path, which reports 0 of 320 wrong every time.
//
// The probe row is overwritten with a dense pseudorandom pattern before
// being sent, because flat content hides displacement. That makes one
// visibly noisy scanline on screen while this is compiled in.
void hal_video_probe_readback(void) {
#if !FEATHER_TFT_READBACK_PROBE
    Serial.println("[readback] probe not compiled in "
                   "(FEATHER_TFT_READBACK_PROBE)");
#else
    if (!s_probe_valid || !s_dma) { Serial.println("[readback] not ready"); return; }
    static uint8_t  raw[320 * 3];
    static uint16_t got[320];

    if (!arch_spi_lcd_read_begin()) { Serial.println("[readback] bus busy"); return; }
    uint8_t p[4] __attribute__((aligned(4)));
    arch_spi_lcd_slow_cmd(ILI9341_CASET);
    p[0] = 0; p[1] = 0; p[2] = (HAL_VIDEO_WIDTH - 1) >> 8; p[3] = (uint8_t)(HAL_VIDEO_WIDTH - 1);
    arch_spi_lcd_slow_write(p, 4);
    arch_spi_lcd_slow_cmd(ILI9341_PASET);
    p[0] = PROBE_ROW >> 8; p[1] = (uint8_t)PROBE_ROW;
    p[2] = PROBE_ROW >> 8; p[3] = (uint8_t)PROBE_ROW;
    arch_spi_lcd_slow_write(p, 4);
    arch_spi_lcd_slow_cmd(0x2E);                 // RAMRD
    arch_spi_lcd_slow_read(raw, 1 + HAL_VIDEO_WIDTH * 3);   // one dummy byte first
    arch_spi_lcd_read_end();

    for (uint32_t i = 0; i < HAL_VIDEO_WIDTH; i++) {
        const uint8_t *q = raw + 1 + i * 3;
        got[i] = (uint16_t)(((q[0] & 0xF8) << 8) | ((q[1] & 0xFC) << 3) | (q[2] >> 3));
    }

    int best_shift = 0; uint32_t best_bad = 0xFFFFFFFFu;
    for (int sh = -40; sh <= 40; sh++) {
        uint32_t bad = 0, n = 0;
        for (int i = 0; i < (int)HAL_VIDEO_WIDTH; i++) {
            int j = i + sh;
            if (j < 0 || j >= (int)HAL_VIDEO_WIDTH) continue;
            n++;
            if (got[i] != s_probe_expect[j]) bad++;
        }
        if (n && bad < best_bad) { best_bad = bad; best_shift = sh; }
    }
    uint32_t bad0 = 0;
    for (uint32_t i = 0; i < HAL_VIDEO_WIDTH; i++)
        if (got[i] != s_probe_expect[i]) bad0++;

    Serial.printf("[readback] raw %02X %02X %02X %02X %02X %02X %02X   "
                  "got %04X %04X %04X   want %04X %04X %04X\n",
                  raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6],
                  got[0], got[1], got[2],
                  s_probe_expect[0], s_probe_expect[1], s_probe_expect[2]);
    Serial.printf("[readback] row %u: exact %lu/320 bad   best shift %+d -> %lu\n",
                  (unsigned)PROBE_ROW, (unsigned long)bad0,
                  best_shift, (unsigned long)best_bad);
    // The read borrowed the bus and left the panel addressed at one row.
    // Put the full-screen window back before the next frame streams.
    lcd_addr_window(0, 0, HAL_VIDEO_WIDTH, HAL_VIDEO_HEIGHT);
    s_in_frame = false;
    s_y = 0;
#endif
}

#endif // ARDUINO_ADAFRUIT_FEATHER_ESP32_V2
