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
#include "soc/spi_reg.h"
#include "esp_rom_lldesc.h"
#include "esp_private/spi_common_internal.h"
#include "esp_private/spi_dma.h"
#include "hal/spi_types.h"

// One descriptor is enough and always will be: the caller is a display
// backend pushing one scanline at a time and a descriptor holds 4092 bytes,
// which is 2046 RGB565 pixels. The header enforces the limit.
static spi_dma_desc_t s_desc __attribute__((aligned(4)));

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

    // BURST MODE OFF -- deliberately, and this is a finding, not a default.
    //
    // It was originally enabled for both the data and the descriptor fetch
    // because it reads as free throughput. It is the one setting in this
    // file capable of corrupting words WITHIN a transfer while leaving the
    // source buffer perfectly intact, which is the exact signature of the
    // artifact this was chased with: rows verified byte-correct in memory
    // (0 of 230,399 in-flight checks failed), no buffer overrun, unaffected
    // by clock from 40MHz down to 13.3MHz, and invisible on any content
    // without within-row variation.
    spi_dma_enable_burst(s_chan, false, false);

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

    // Descriptor built by the IDF's own helper rather than by hand. The
    // hand-rolled version set the documented fields correctly as far as
    // reading the TRM goes, and still produced a row displaced by exactly
    // 16 pixels -- so "as far as reading the TRM goes" was not far enough.
    spicommon_dma_desc_setup_link(&s_desc, data, (int)len, false);

    spi_dma_reset(s_chan);

    // DMA FIFO RESET -- a SEPARATE operation from the channel reset above,
    // and the one this file was missing.
    //
    // spi_dma_reset() resets the DMA channel. The path from that engine
    // into the SPI transmit FIFO is the AHB master, which has its own two
    // reset bits, and they must be raised TOGETHER and dropped TOGETHER:
    // that is what the IDF's spi_ll_dma_tx_fifo_reset() does, via
    // SPI_LL_DMA_FIFO_RST_MASK. Pulsing them one at a time -- which is what
    // this code did, on the reasoning that it matched the channel reset's
    // sequential style -- does not clear the FIFO.
    //
    // The symptom of getting it wrong is worth recording, because nothing
    // about it says "FIFO": every scanline arrived INTACT but displaced
    // exactly 16 pixels (32 bytes) to the right, with the last 32 bytes
    // dropped. Each burst left 32 bytes staged; the next burst clocked
    // those out first and left 32 of its own behind, so the lag sustained
    // itself forever at exactly one FIFO-load.
    //
    // Read back off the panel's own RAM to establish that: best shift -16
    // gave 0 mismatches of 320, against 0/320 exact on the CPU-FIFO path.
    // Invisible on flat content -- an all-black frame and a flat-stripe
    // frame both looked perfect -- which is why it survived as unexplained
    // "vertical rain" over real game graphics.
    dev->dma_conf.val |=  (SPI_AHBM_RST | SPI_AHBM_FIFO_RST);
    dev->dma_conf.val &= ~(SPI_AHBM_RST | SPI_AHBM_FIFO_RST);

    // Clear the whole outlink register before arming it. spi_dma_start()
    // writes the address and the start bit as a read-modify-write, so the
    // stop bit arch_spi_dma_wait() set would otherwise survive and the
    // transfer would never begin.
    dev->dma_out_link.val = 0;

    // HALF-DUPLEX, TRANSMIT ONLY, FOR THE DURATION OF THE BURST.
    //
    // The Arduino core sets user.doutdin and user.usr_miso once when the bus
    // starts and never clears them, so the peripheral sits in full duplex.
    // That is harmless on the CPU-FIFO path -- unwanted receive bits land in
    // data_buf and nobody reads them, which is why spiWritePixelsNL() can
    // ignore the whole question. It is NOT harmless with DMA: a TX link
    // armed, full duplex selected and NO RX link is a combination the IDF
    // driver never produces, and it corrupts occasional transfers. That was
    // the "vertical rain" -- single scanlines arriving wrong, drifting
    // across the picture, visible even on a frozen frame where the same
    // bytes were sent every time.
    //
    // Restored in arch_spi_dma_wait(), because SPIClass and everything built
    // on it expect to find the bus as they left it.
    dev->user.doutdin  = 0;
    dev->user.usr_miso = 0;

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
    // on an EOF flag instead would let the caller deassert CS or overwrite
    // the buffer with bytes still queued.
    //
    // This IS the whole completion test, and that is not an assumption --
    // the IDF's own spi_hal_usr_is_done() checks exactly this and nothing
    // else. An earlier version of this file also spun on
    // dma_int_raw.out_total_eof, reasoning that the SPI block going idle
    // need not mean the DMA engine had retired the descriptor. That bit
    // never fires for a master TX-only burst on this silicon, so the spin
    // never exited and the board hung after asset load with no frames at
    // all. If a stricter completion check is ever really needed, out_eof is
    // the one to try, and it needs a bounded spin so a wrong guess cannot
    // hang the machine again.
    while (dev->cmd.usr) { }

    // Hand the peripheral back. Without this the next CPU-FIFO write on this
    // bus -- an ILI9341 command, or SdFat reading the card -- transmits
    // whatever the DMA engine left staged instead of the bytes it was given.
    dev->dma_out_link.stop  = 1;
    dev->dma_out_link.start = 0;
    spi_dma_reset(s_chan);
    dev->dma_int_clr.val = 0xFFFFFFFFu;

    // Full duplex back on, as the Arduino core left it. See write_async().
    dev->user.doutdin  = 1;
    dev->user.usr_miso = 1;

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
