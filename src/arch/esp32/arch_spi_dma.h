// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// ESP32 SPI transmit-DMA. See arch/arch.h for why this is arch and not board.
//
// WHY THIS EXISTS. Adafruit_SPITFT::writePixels() ends up in the Arduino
// core's spiWritePixelsNL(), which is a 64-byte poll loop: for every 64
// bytes it writes 16 words into the SPI FIFO, sets the transfer length,
// starts the transaction and spins until it finishes. At 40MHz a 320-pixel
// row is 128us of actual clocking, but ten separate FIFO round-trips per
// row cost far more than that -- measured 44.6ms for a 320x240 frame
// against a 30.7ms wire time, so 31% of the frame was overhead.
//
// The DMA engine reads the row straight out of DRAM in one transfer, which
// removes that overhead AND, more importantly, RETURNS IMMEDIATELY. The
// caller renders the next scanline while this one is still going out. On a
// board where the panel is the bottleneck that second property is worth
// more than the first.
//
// THIS IS THE SAME SPLIT AS THE AUDIO PATH, for the same reason. The panel
// is configured by Adafruit_ILI9341 -- init sequence, rotation, address
// window, all library. What no library covers is the chip's DMA engine, so
// that part is hand-written and lives here, exactly as the RP2 I2S PIO
// transport does next to Adafruit_TLV320_I2S. See
// src/arch/rp2040/arch_audio_i2s.h.
//
// COEXISTENCE WITH SPIClass IS THE POINT. This drives the same peripheral
// the Arduino SPI object already configured -- same clock, same mode, same
// pins, and CS asserted by Adafruit_SPITFT's startWrite(). It only borrows
// the data path, and arch_spi_dma_wait() hands the peripheral back in
// CPU-FIFO state. That is what lets Adafruit_ILI9341 keep sending commands
// and SdFat keep reading the card on the same bus with no handover
// choreography.
#ifndef ARCADE_ARCH_ESP32_SPI_DMA_H
#define ARCADE_ARCH_ESP32_SPI_DMA_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Claim a DMA channel and bind it to an SPI host. `spi_host` is the Arduino
// core's bus number: 2 = HSPI, 3 = VSPI (the Arduino global `SPI` object is
// VSPI on the original ESP32). Call once, AFTER the Arduino SPI bus is up --
// this reads none of its configuration but has no reason to run before it.
//
// Returns false if the channel could not be claimed or the target is not the
// original ESP32. A caller that gets false must keep using the FIFO path;
// nothing here half-works.
bool arch_spi_dma_init(int spi_host);

// True once arch_spi_dma_init() has succeeded.
bool arch_spi_dma_available(void);

// Start a transmit-only burst and RETURN IMMEDIATELY. Three requirements,
// all of them silent to break:
//
//   - `data` must be 4-byte-aligned and in internal DRAM. Not PSRAM, not a
//     flash constant -- this DMA engine reaches neither.
//   - `data` must stay untouched until arch_spi_dma_wait() returns. Double
//     buffer; that is the whole point.
//   - Chip select must already be asserted (Adafruit_SPITFT::startWrite()),
//     and the bytes must already be in wire order. DMA reads memory
//     verbatim, so a caller pushing RGB565 does its own byte swap -- the
//     FIFO path did that swap for you and this one cannot.
//
// `len` is in bytes and must not exceed 4092 (one descriptor).
void arch_spi_dma_write_async(const void *data, size_t len);

// Block until the last burst has finished CLOCKING OUT (not merely until
// the DMA engine drained into the FIFO), then return the peripheral to the
// CPU-FIFO path. Safe to call with nothing in flight. After this returns,
// ordinary SPIClass traffic on the same bus works normally.
void arch_spi_dma_wait(void);

#ifdef __cplusplus
}
#endif

#endif // ARCADE_ARCH_ESP32_SPI_DMA_H
