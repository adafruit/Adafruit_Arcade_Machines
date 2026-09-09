// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// ESP32 SPI transmit-DMA -- see arch_spi_dma.h for what this is and why it
// is arch rather than board.
//
// The whole file is guarded: Arduino compiles every source under src/
// regardless of the selected board, so an arch .cpp has to exclude itself.
#if defined(ARDUINO_ARCH_ESP32)

// Arduino.h first, for sdkconfig.h and therefore CONFIG_IDF_TARGET_*. The
// outer guard cannot test the target directly because that macro does not
// exist on the compiler command line.
#include <Arduino.h>

#include "arch/esp32/arch_spi_dma.h"

#if CONFIG_IDF_TARGET_ESP32

#include "soc/spi_struct.h"
#include "esp_rom_lldesc.h"
#include "esp_private/spi_common_internal.h"
#include "esp_private/spi_dma.h"
#include "hal/spi_types.h"

// One descriptor is enough and always will be: the caller is a display
// backend pushing one scanline at a time and a descriptor holds 4092 bytes,
// which is 2046 RGB565 pixels. The header enforces the limit.
static lldesc_t s_desc __attribute__((aligned(4)));

static volatile spi_dev_t   *s_dev  = NULL;
static spi_dma_chan_handle_t s_chan;
static bool                  s_busy = false;

bool arch_spi_dma_available(void) { return s_dev != NULL; }

bool arch_spi_dma_init(int spi_host) {
    if (s_dev) return true;

    // SPI2/SPI3 are the two general-purpose hosts; SPI0/SPI1 are the flash
    // controller and are not ours to touch. The struct instances come from
    // the SoC linker script, which is also where the IDF's own low-level
    // layer gets them.
    spi_host_device_t host_id;
    volatile spi_dev_t *dev;
    if (spi_host == 3)      { host_id = SPI3_HOST; dev = &SPI3; }
    else if (spi_host == 2) { host_id = SPI2_HOST; dev = &SPI2; }
    else return false;

    // The supported way in. This enables the SPI DMA clock, picks a free
    // channel, refcounts it so nothing else the IDF hands a channel to can
    // be given the same one, and -- the part that is easy to miss -- writes
    // the DPORT register OUTSIDE the SPI block that decides which of the two
    // channels feeds which host. Doing all of that by hand would work today
    // and collide later.
    spi_dma_ctx_t *dma_ctx = NULL;
    if (spicommon_dma_chan_alloc(host_id, SPI_DMA_CH_AUTO, &dma_ctx) != ESP_OK) {
        return false;
    }
    s_chan = dma_ctx->tx_dma_chan;

    // Burst mode for both the data and the descriptor fetch. Persists for the
    // life of the channel, so it is set once here rather than per transfer.
    spi_dma_enable_burst(s_chan, true, true);

    s_dev  = dev;
    s_busy = false;
    return true;
}

void arch_spi_dma_write_async(const void *data, size_t len) {
    volatile spi_dev_t *dev = s_dev;
    if (!dev || len == 0) return;

    // Anything the CPU-FIFO path started -- a command byte from
    // Adafruit_ILI9341 -- must be off the wire before the peripheral is
    // reconfigured underneath it.
    while (dev->cmd.usr) { }

    s_desc.size   = len;
    s_desc.length = len;
    s_desc.offset = 0;
    s_desc.sosf   = 0;
    s_desc.eof    = 1;       // single descriptor, so it is also the last
    s_desc.owner  = 1;       // hardware owns it until the burst completes
    s_desc.buf    = (const uint8_t *)data;
    s_desc.qe.stqe_next = NULL;

    spi_dma_reset(s_chan);

    // Clear the whole outlink register before arming it. spi_dma_start()
    // writes the address and the start bit as a read-modify-write, so the
    // stop bit arch_spi_dma_wait() set would otherwise survive and the
    // transfer would never begin.
    dev->dma_out_link.val = 0;

    // Length is set on the SPI side, not the DMA side: the descriptor says
    // how much to fetch, this says how many bits to clock, and they must
    // agree. usr_miso_dbitlen is zeroed for the same reason
    // spiWritePixelsNL() zeroes it -- the Arduino core leaves the bus in
    // full-duplex mode and a nonzero RX length would clock extra bits.
    dev->mosi_dlen.usr_mosi_dbitlen = (len * 8) - 1;
    dev->miso_dlen.usr_miso_dbitlen = 0;

    spi_dma_start(s_chan, &s_desc);
    dev->cmd.usr = 1;

    s_busy = true;
}

void arch_spi_dma_wait(void) {
    if (!s_busy) return;
    volatile spi_dev_t *dev = s_dev;

    // cmd.usr clears when the last bit is on the wire, not when the DMA
    // engine finished filling the FIFO. That distinction matters: returning
    // on the EOF interrupt instead would let the caller deassert CS or
    // overwrite the buffer with bytes still queued.
    while (dev->cmd.usr) { }

    // Hand the peripheral back. Without this the next CPU-FIFO write on this
    // bus -- an ILI9341 command, or SdFat reading the card -- transmits
    // whatever the DMA engine left staged instead of the bytes it was given.
    dev->dma_out_link.stop  = 1;
    dev->dma_out_link.start = 0;
    spi_dma_reset(s_chan);

    s_busy = false;
}

#else // !CONFIG_IDF_TARGET_ESP32

// Every other ESP32 variant. The S2/S3/C3/C6 route SPI through GDMA, a
// completely different engine with a different register block, so the code
// above is not merely untested there -- it does not apply. Fail at init
// rather than silently, so a caller falls back to the FIFO path with a
// reason instead of pushing pixels into a DMA engine that was never armed.
bool arch_spi_dma_init(int spi_host)  { (void)spi_host; return false; }
bool arch_spi_dma_available(void)     { return false; }
void arch_spi_dma_write_async(const void *data, size_t len) { (void)data; (void)len; }
void arch_spi_dma_wait(void)          { }

#endif // CONFIG_IDF_TARGET_ESP32
#endif // ARDUINO_ARCH_ESP32
