// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// See usb_host_fruitjam.h.
#include "boards/fruitjam/usb_host_fruitjam.h"

#if defined(ARDUINO_ADAFRUIT_FRUITJAM_RP2350) && defined(USE_TINYUSB)

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <pio_usb.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>

namespace {

// Video uses PIO 0 and audio PIO 1 (arch_audio_i2s_rp2040.cpp).
constexpr uint8_t kUsbPio = 2;

Adafruit_USBH_Host g_host;
int  g_dma = -1;
bool g_started = false;

} // namespace

bool fruitjam_usb_host_begin(void) {
    if (g_started) return true;
    if (clock_get_hz(clk_sys) % 12000000u != 0) return false;

    pinMode(PIN_5V_EN, OUTPUT);
    digitalWrite(PIN_5V_EN, PIN_5V_EN_STATE); // power the Type-A ports

    pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp = PIN_USB_HOST_DP;
    cfg.pio_tx_num = kUsbPio;
    cfg.pio_rx_num = kUsbPio;
    // Pico PIO USB claims the channel it is given and defaults to channel
    // 0, which the display may already own: hand it a free one.
    g_dma = dma_claim_unused_channel(true);
    dma_channel_unclaim((uint)g_dma);
    cfg.tx_ch = (uint8_t)g_dma;
    g_host.configure_pio_usb(1, &cfg);
    g_host.begin(1);
    g_started = true;
    return true;
}

void fruitjam_usb_host_task(void) {
    if (g_started) g_host.task();
}

int fruitjam_usb_host_dma_channel(void) { return g_dma; }

#endif
