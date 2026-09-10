// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// ESP32 SPI LCD transport -- see arch_spi_dma.h for what this is, why it
// uses the IDF driver rather than the registers, and why the bus is handed
// over exactly once.
//
// The whole file is guarded: Arduino compiles every source under src/
// regardless of the selected board, so an arch .cpp has to exclude itself.
#if defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include "arch/esp32/arch_spi_dma.h"

#include <SPI.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"

static spi_device_handle_t s_dev  = NULL;
// A second device on the same bus, clocked far slower. The ILI9341 writes
// happily at 40MHz but its READ timing is much looser -- reads at 40MHz come
// back as garbage -- and the driver lets one bus carry devices at different
// clocks, so the diagnostic read path just uses its own.
static spi_device_handle_t s_dev_slow = NULL;
static bool                s_ready = false;
static int                 s_dc   = -1;

// Two descriptors, alternated, so one transfer can be in flight while the
// next scanline is being rendered. `s_pending` counts what the driver still
// owes us; nothing may touch a buffer until its transfer has been reaped.
static spi_transaction_t s_trans[2];
static int               s_ti = 0;
static int               s_pending = 0;

// Runs before each transfer, including from the driver's ISR, so it must be
// cheap and must not call into anything that could block. `user` carries the
// DC level for that transfer: the command/data distinction is per-transfer,
// which is exactly what this callback exists for.
static void IRAM_ATTR lcd_pre_cb(spi_transaction_t *t) {
    gpio_set_level((gpio_num_t)s_dc, (int)(intptr_t)t->user);
}

bool arch_spi_lcd_ready(void) { return s_ready; }

bool arch_spi_lcd_begin(int spi_host, int sck, int mosi, int miso,
                        int cs, int dc, int clock_hz) {
    if (s_ready) return true;

    spi_host_device_t host;
    if (spi_host == 3)      host = SPI3_HOST;
    else if (spi_host == 2) host = SPI2_HOST;
    else return false;

    // Release Arduino's claim first. Everything that used SPIClass -- the
    // panel init sequence and the SD card -- is finished by now, and the
    // two libraries cannot both own the peripheral.
    SPI.end();

    s_dc = dc;
    gpio_reset_pin((gpio_num_t)dc);
    gpio_set_direction((gpio_num_t)dc, GPIO_MODE_OUTPUT);
    pinMode(cs, OUTPUT);
    digitalWrite(cs, LOW);                  // asserted for good; see below

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num     = mosi;
    buscfg.miso_io_num     = miso;
    buscfg.sclk_io_num     = sck;
    buscfg.quadwp_io_num   = -1;
    buscfg.quadhd_io_num   = -1;
    // One scanline is the largest thing sent, plus headroom.
    buscfg.max_transfer_sz = 4096;
    if (spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) return false;

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = clock_hz;
    devcfg.mode           = 0;              // ILI9341 is SPI mode 0
    // CHIP SELECT IS DRIVEN BY HAND, not by the driver.
    //
    // Handing the driver spics_io_num left the panel completely deaf: a
    // command-only invert self-test produced no flash at all, which means
    // nothing was being selected. Rather than work out why the hardware CS
    // would not take pin 15 over from the GPIO output Adafruit_ILI9341 left
    // driving it, CS is simply held low here for the life of the program.
    // Nothing else is on this bus after the handover, so it never needs to
    // rise, and this is what Adafruit_SPITFT does anyway.
    devcfg.spics_io_num   = -1;
    devcfg.queue_size     = 4;
    devcfg.pre_cb         = lcd_pre_cb;
    devcfg.flags          = SPI_DEVICE_NO_DUMMY;
    if (spi_bus_add_device(host, &devcfg, &s_dev) != ESP_OK) return false;

    // CS IS LEFT TO THE DRIVER, one assertion per transaction.
    //
    // The tempting alternative -- acquire the bus and set
    // SPI_TRANS_CS_KEEP_ACTIVE on everything, so CS never rises -- was
    // tried first, on the reasoning that dropping CS between RAMWR and its
    // pixel data would abort the memory write. It produced a black screen.
    // The ILI9341 keeps its write pointer across CS transitions, which is
    // why every IDF LCD example and galagino itself simply let the driver
    // toggle it.

    // Diagnostic read device. Failure here is not fatal -- it only costs
    // the readback probe.
    spi_device_interface_config_t slowcfg = devcfg;
    slowcfg.clock_speed_hz = 6000000;
    slowcfg.queue_size     = 1;
    (void)spi_bus_add_device(host, &slowcfg, &s_dev_slow);

    s_pending = 0;
    s_ti = 0;
    s_ready = true;
    return true;
}

// Reap completed transfers until at most `keep` remain outstanding.
static void reap(int keep) {
    spi_transaction_t *done;
    while (s_pending > keep) {
        if (spi_device_get_trans_result(s_dev, &done, portMAX_DELAY) != ESP_OK) break;
        s_pending--;
    }
}

static void send_blocking(const void *data, size_t len, int dc_level) {
    if (!s_ready || len == 0) return;
    reap(0);                                  // ordering: this must go last
    spi_transaction_t t = {};
    t.length    = len * 8;
    t.tx_buffer = data;
    t.user      = (void *)(intptr_t)dc_level;
    spi_device_polling_transmit(s_dev, &t);
}

void arch_spi_lcd_cmd(uint8_t cmd) {
    static uint8_t b __attribute__((aligned(4)));
    b = cmd;
    send_blocking(&b, 1, 0);
}

void arch_spi_lcd_data(const void *data, size_t len) {
    send_blocking(data, len, 1);
}

void arch_spi_lcd_data_async(const void *data, size_t len) {
    if (!s_ready || len == 0) return;
    // QUEUE DEPTH AND BUFFER COUNT ARE THE SAME NUMBER.
    //
    // Two transfers may be in flight, which means the driver can own two
    // buffers at once -- so the caller must provide three, leaving one for
    // the renderer. src/boards/feather_esp32 does. Getting this wrong is
    // not subtle on screen: with two buffers and two queued transfers the
    // renderer overwrites a buffer mid-send, and the frame is clean at the
    // top, broken through the middle, with colours shifted between
    // adjacent RGB565 fields. See DEVNOTES #109.
    reap(1);
    spi_transaction_t *t = &s_trans[s_ti];
    *t = spi_transaction_t{};
    t->length    = len * 8;
    t->tx_buffer = data;
    t->user      = (void *)(intptr_t)1;       // DC high: pixel data
    if (spi_device_queue_trans(s_dev, t, portMAX_DELAY) == ESP_OK) {
        s_pending++;
        s_ti ^= 1;
    }
}

void arch_spi_lcd_flush(void) { if (s_ready) reap(0); }

// ---------------------------------------------------------------------------
// Diagnostic read path. Everything below exists to answer "what did the
// panel actually receive", which is the only question that made the
// register-level transport's 16-pixel displacement tractable (DEVNOTES
// #108). Reads run on the slow device, so the fast one must give up the bus
// for the duration.
bool arch_spi_lcd_read_begin(void) {
    if (!s_ready || !s_dev_slow) return false;
    reap(0);
    return true;
}

void arch_spi_lcd_read_end(void) {
    if (!s_ready || !s_dev_slow) return;
    /* nothing to release: CS is per-transaction */
}

void arch_spi_lcd_slow_cmd(uint8_t cmd) {
    if (!s_dev_slow) return;
    spi_transaction_t t = {};
    t.length     = 8;
    t.tx_data[0] = cmd;
    t.user       = (void *)(intptr_t)0;          // DC low
    t.flags      = SPI_TRANS_USE_TXDATA;
    spi_device_polling_transmit(s_dev_slow, &t);
}

void arch_spi_lcd_slow_write(const void *data, size_t len) {
    if (!s_dev_slow || !len) return;
    spi_transaction_t t = {};
    t.length    = len * 8;
    t.tx_buffer = data;
    t.user      = (void *)(intptr_t)1;           // DC high
    spi_device_polling_transmit(s_dev_slow, &t);
}

void arch_spi_lcd_slow_read(void *dst, size_t len) {
    if (!s_dev_slow || !len) return;
    spi_transaction_t t = {};
    t.length    = len * 8;                       // clocks to generate
    t.rxlength  = len * 8;
    t.rx_buffer = dst;
    t.user      = (void *)(intptr_t)1;           // DC high
    spi_device_polling_transmit(s_dev_slow, &t);
}

#endif // ARDUINO_ARCH_ESP32
