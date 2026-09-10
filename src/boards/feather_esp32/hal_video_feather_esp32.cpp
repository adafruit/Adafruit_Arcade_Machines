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

    // After begin(), so the bus is configured before a channel is bound to
    // it. Failure is not fatal: the FIFO path still works, just slowly, and
    // a board that renders at half speed is far easier to diagnose than one
    // that shows nothing.
    // DMA IS DISABLED, AND THIS IS NOT A PERFORMANCE CHOICE.
    //
    // The transport works and is fast -- 31.8fps against the FIFO path's
    // 16.8 -- but it puts every scanline on the panel displaced by exactly
    // 16 pixels. There is a CONSTANT 32-BYTE LAG in the DMA-to-SPI path:
    // each burst emits the 32 bytes staged by the previous burst, then the
    // first 608 of its own, staging its own last 32. The lag does not
    // accumulate, because the panel still receives exactly 640 bytes per
    // row -- it just holds a permanent 16-pixel phase error.
    //
    // Measured, not inferred: reading the panel's own RAM back gives
    // "best shift -16 -> 0 mismatches of 320" on this path and "0/320
    // exact" on the CPU-FIFO path, using the same probe.
    //
    // WHY IT HID FOR SO LONG. A 16-pixel slide is invisible on flat
    // content, so an all-black frame and a frame of flat-coloured stripes
    // both looked perfect. Only real graphics reveal it: each row's first
    // 16 pixels are the PREVIOUS row's last 16, which on a rotated panel
    // shows as a thin strip along one edge one row out of step, plus
    // sparse speckle everywhere else.
    //
    // RULED OUT, each by a test that discriminated rather than confirmed:
    // link margin (present at 40, 26.7 and 13.3MHz), full duplex, CPU/DMA
    // concurrency (synchronous DMA shows it too), renderer overrun (fenced
    // buffers stayed intact), in-flight buffer corruption (0 of 230,399
    // checksum pairs), DMA burst mode, the descriptor (IDF's own
    // spicommon_dma_desc_setup_link makes no difference), the CPU-side
    // FIFO (a sentinel written there never appears on the wire), and
    // IDF's combined SPI_AHBM_RST|SPI_AHBM_FIFO_RST reset.
    //
    // ALSO TRIED AND FAILED: sending the ILI9341 commands over DMA so the
    // peripheral never switches source -- single-byte DMA bursts do not
    // come out of this engine at all and the panel went black; and a
    // throwaway burst with CS held high, which cannot work against a
    // constant lag for the reason given above.
    //
    // Re-enable by restoring the assignment below. The readback probe is
    // the tool to use -- calibrate it against the FIFO path first, because
    // an instrument that has not been shown to read zero on a known-good
    // path is worth nothing.
    (void)arch_spi_dma_init;
    s_dma = false;
    return true;
}

// THE HANDOVER FLUSH.
//
// The first DMA burst after any CPU-FIFO write puts 32 stale bytes on the
// wire ahead of the data it was given. Measured by reading the panel's own
// RAM back: every scanline arrives INTACT but displaced exactly 16 pixels,
// with its last 32 bytes dropped -- "best shift -16 -> 0 mismatches of
// 320", against 0/320 exact on the pure CPU-FIFO path.
//
// It hides well. A 16-pixel slide is invisible on flat content, so an
// all-black frame and a frame of flat-coloured stripes both looked
// perfect; only real game graphics show it, as sparse speckle. On a
// rotated panel it also puts each row's first 16 pixels one row out of
// step, which reads as a thin strip along one screen edge offset from the
// rest of the picture.
//
// The cure is to let that first burst happen where the panel is not
// listening. CS IS A PLAIN GPIO UNDER SOFTWARE CONTROL, so it can be
// raised without ending the SPI transaction -- which matters, because an
// earlier attempt parked CS with endWrite()/startWrite() and failed twice
// over: ending the transaction meant the flush burst probably never went
// out, and startWrite() afterwards re-triggered the very handover it was
// supposed to absorb.
static uint8_t s_flush[32] __attribute__((aligned(4)));

static void dma_handover_flush(void) {
    if (!s_dma) return;
    arch_spi_dma_wait();
    digitalWrite(FEATHER_TFT_CS, HIGH);   // panel stops listening
    arch_spi_dma_write_async(s_flush, sizeof s_flush);
    arch_spi_dma_wait();
    digitalWrite(FEATHER_TFT_CS, LOW);    // bus clean, panel listening again
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
        // Both of the above went out over the CPU-FIFO path, so the next
        // DMA burst would carry 32 stale bytes. Spend them with CS high.
        dma_handover_flush();
        s_in_frame = true;
        s_y = 0;
    }

#if FEATHER_TFT_READBACK_PROBE
    if (s_y == PROBE_ROW) {
        // Dense pseudorandom pattern: flat content hides displacement, and
        // that is exactly how this artifact stayed hidden. Costs one noisy
        // scanline on screen while the probe is compiled in.
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
    if (!s_probe_valid) { Serial.println("[readback] no probe row yet"); return; }
    static uint16_t got[320];

    s_tft.setSPISpeed(6000000);      // the panel reads far slower than it writes
    s_tft.startWrite();
    s_tft.setAddrWindow(0, PROBE_ROW, HAL_VIDEO_WIDTH, 1);
    s_tft.writeCommand(0x2E);        // RAMRD
    (void)s_tft.spiRead();           // one dummy byte before pixel data
    for (uint16_t i = 0; i < HAL_VIDEO_WIDTH; i++) {
        uint8_t r = s_tft.spiRead(), g = s_tft.spiRead(), b = s_tft.spiRead();
        got[i] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
    s_tft.endWrite();
    s_tft.setSPISpeed(40000000);

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

    Serial.printf("[readback] row %u: exact %lu/320 bad   best shift %+d -> %lu\n",
                  (unsigned)PROBE_ROW, (unsigned long)bad0,
                  best_shift, (unsigned long)best_bad);
#endif
}

#endif // ARDUINO_ADAFRUIT_FEATHER_ESP32_V2
