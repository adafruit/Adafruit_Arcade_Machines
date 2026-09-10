// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// ESP32 SPI LCD transport, on ESP-IDF's spi_master driver.
//
// WHY THIS EXISTS. Adafruit_SPITFT::writePixels() ends up in the Arduino
// core's spiWritePixelsNL(), a 64-byte poll loop: for every 64 bytes it
// writes 16 words into the SPI FIFO, sets the length, starts a transaction
// and spins. At 40MHz a 320-pixel row is 128us of actual clocking, but ten
// FIFO round-trips per row cost far more -- measured 44.6ms for a 320x240
// frame against a 30.7ms wire time. It also cannot overlap: the CPU is busy
// pushing bytes and can do nothing else.
//
// WHY THE DRIVER AND NOT REGISTERS. An earlier version of this file drove
// the SPI DMA engine directly. It was fast (31.8fps against 17.7) and
// wrong: every scanline landed 16 pixels out, a constant 32-byte lag in the
// DMA-to-SPI path that survived nine separate attempts to flush it. See
// DEVNOTES #108 for the full autopsy. galagino runs this same panel at this
// same 40MHz through the IDF driver and reaches ~30Hz -- essentially the
// wire limit -- which is the proof that the silicon is fine and the
// hand-written register sequence was not. Using the vendor's driver is also
// simply the right call for this library: the same reasoning that put SdFat
// and Adafruit_TLV320 in place of hand-rolled equivalents.
//
// BUS OWNERSHIP, AND WHY THERE IS A HANDOVER. The IDF driver wants the SPI
// bus to itself, but Adafruit_ILI9341 needs it to initialise the panel and
// SdFat needs it to read the ROMs -- both through Arduino's SPIClass. Those
// happen once, at boot, in that order, and neither is touched again. So the
// bus is handed over exactly once, at hal_video_run(), which the HAL
// already defines as "the caller is ready to feed scanlines continuously".
// Before it: SPIClass. After it: the IDF driver, forever.
//
// The panel's own configuration -- init sequence, rotation -- stays with
// Adafruit_ILI9341, which runs before the handover. Only the three address
// commands and the pixel stream live here, because after the handover
// nothing else can reach the bus.
#ifndef ARCADE_ARCH_ESP32_SPI_DMA_H
#define ARCADE_ARCH_ESP32_SPI_DMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Take the SPI bus from Arduino's SPIClass and hand it to the IDF driver.
// Call ONCE, after every SPIClass user is finished with the bus for good --
// panel init and asset loading both included. `spi_host` is the Arduino bus
// number: 2 = HSPI, 3 = VSPI (the global `SPI` object is VSPI on the
// original ESP32).
//
// Returns false if the bus or device could not be claimed, in which case
// the caller must keep using its existing path; nothing here half-works.
bool arch_spi_lcd_begin(int spi_host, int sck, int mosi, int miso,
                        int cs, int dc, int clock_hz);

// True once arch_spi_lcd_begin() has succeeded.
bool arch_spi_lcd_ready(void);

// One command byte (DC low). Blocking.
void arch_spi_lcd_cmd(uint8_t cmd);

// Data bytes (DC high). Blocking; for short payloads such as address
// coordinates.
void arch_spi_lcd_data(const void *data, size_t len);

// Queue data bytes (DC high) and RETURN IMMEDIATELY. Two requirements:
//
//   - `data` must stay untouched until a later arch_spi_lcd_flush() has
//     retired it. Double buffer; that is the whole point.
//   - `data` must be in internal DRAM, not PSRAM.
//
// At most two transfers are in flight; queueing a third blocks until one
// retires, which is the natural pacing for a scanline pump.
void arch_spi_lcd_data_async(const void *data, size_t len);

// Block until every queued transfer has completed.
void arch_spi_lcd_flush(void);

// --- Diagnostic read path --------------------------------------------------
// Reads run on a slower second device, so the fast one gives up the bus for
// the duration. Bracket every read with begin/end. Only useful for a panel
// that supports reading its own RAM back; see DEVNOTES #108 for what that
// bought.
bool arch_spi_lcd_read_begin(void);
void arch_spi_lcd_read_end(void);
void arch_spi_lcd_slow_cmd(uint8_t cmd);
void arch_spi_lcd_slow_write(const void *data, size_t len);
void arch_spi_lcd_slow_read(void *dst, size_t len);

#ifdef __cplusplus
}
#endif

#endif // ARCADE_ARCH_ESP32_SPI_DMA_H
