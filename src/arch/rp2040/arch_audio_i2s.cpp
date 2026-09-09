// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// RP2040/RP2350 I2S transmit -- see arch_audio_i2s.h for what this is and
// why it is arch rather than board. Moved verbatim out of
// src/boards/fruitjam/hal_audio_fruitjam.cpp; the only change is that the
// two pin numbers are now parameters instead of FRUITJAM_* macros.
//
// The whole file is guarded: Arduino compiles every source under src/
// regardless of the selected board, so an arch .cpp has to exclude itself.
#if defined(ARDUINO_ARCH_RP2040) || defined(PICO_ON_DEVICE)

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "arch/rp2040/audio_i2s.pio.h"
#include "arch/rp2040/arch_audio_i2s.h"

// pio1, SM 0: DVI uses pio0 (see src/boards/fruitjam/hal_video_fruitjam.cpp),
// so no conflict.
#define AUDIO_PIO      pio1
#define AUDIO_SM       0

// 256 samples, double-buffered. This was briefly lowered to 128 and then 64
// while chasing Lunar Rescue's red lines (DEVNOTES.md problem #34), on the
// reasoning that this ISR runs on Core 0, preempts the scanline render/submit
// pump, and PicoDVI's valid-scanline queue is a hard-capped 8 buffers -- only
// ~555us of slack -- so a long ISR can starve it. That reasoning was sound
// and the measurements were real: worst-single ISR cost fell 232us -> 81us ->
// 40-52us.
//
// It has been PUT BACK, and the reason is worth keeping. Shortening this was
// only ever an interim mitigation for Lunar Rescue, whose real fault was a
// ~1.8ms un-interleaved CPU burst leaving ~200us of margin. Interleaving that
// (problem #34) took the margin to milliseconds, at which point a 232us ISR
// is irrelevant -- but the mitigation's COST did not go away with its
// purpose. More, shorter calls pay the same fixed per-invocation overhead
// more often: measured on Galaga, the game with the least headroom, 64
// samples cost +400us mean / +660us peak per frame versus 256, and a red line
// appeared on hardware during heavy sprite activity with the player firing.
//
// **General lesson: when a real fix lands, remove the interim mitigation and
// re-measure. A workaround's cost outlives its purpose silently.** If a long
// ISR ever looks implicated again, measure `work` in that sketch's heartbeat
// first -- the frame budget is where this actually shows up.
#define BUFFER_SAMPLES 256

#include "arch/arch.h" // ARCADE_FAST_FUNC

static int32_t audio_buf[2][BUFFER_SAMPLES];
static int dma_ch_a, dma_ch_b;
static volatile hal_audio_fill_cb g_fill_cb = NULL;

static void ARCADE_FAST_FUNC(audio_dma_irq_handler)(void) {
    if (dma_irqn_get_channel_status(1, dma_ch_a)) {
        dma_irqn_acknowledge_channel(1, dma_ch_a);
        if (g_fill_cb) g_fill_cb(audio_buf[0], BUFFER_SAMPLES);
        else memset(audio_buf[0], 0, sizeof(audio_buf[0]));
        dma_channel_set_read_addr(dma_ch_a, audio_buf[0], false);
        dma_channel_set_trans_count(dma_ch_a, BUFFER_SAMPLES, false);
    }
    if (dma_irqn_get_channel_status(1, dma_ch_b)) {
        dma_irqn_acknowledge_channel(1, dma_ch_b);
        if (g_fill_cb) g_fill_cb(audio_buf[1], BUFFER_SAMPLES);
        else memset(audio_buf[1], 0, sizeof(audio_buf[1]));
        dma_channel_set_read_addr(dma_ch_b, audio_buf[1], false);
        dma_channel_set_trans_count(dma_ch_b, BUFFER_SAMPLES, false);
    }
}

// ---------------------------------------------------------------------------
// I2S PIO + DMA init
// ---------------------------------------------------------------------------

bool arch_i2s_init(uint32_t sample_rate, uint32_t pin_din, uint32_t pin_bclk) {
    uint offset = pio_add_program(AUDIO_PIO, &audio_i2s_program);
    audio_i2s_program_init(AUDIO_PIO, AUDIO_SM, offset,
                            pin_din, pin_bclk);

    // Clock divider: sys_clock / (sample_rate * 64)
    {
        uint32_t sys_hz = clock_get_hz(clk_sys);
        uint32_t target = sample_rate * 64u;
        uint32_t div_int  = sys_hz / target;
        uint32_t div_frac = (uint32_t)(((uint64_t)(sys_hz % target) * 256u) / target);
        pio_sm_set_clkdiv_int_frac(AUDIO_PIO, AUDIO_SM,
                                   (uint16_t)div_int, (uint8_t)div_frac);
    }

    dma_ch_a = dma_claim_unused_channel(true);
    dma_ch_b = dma_claim_unused_channel(true);
    memset(audio_buf, 0, sizeof(audio_buf));

    dma_channel_config cfg = dma_channel_get_default_config(dma_ch_a);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(AUDIO_PIO, AUDIO_SM, true));
    channel_config_set_chain_to(&cfg, dma_ch_b);
    dma_channel_configure(dma_ch_a, &cfg,
        &AUDIO_PIO->txf[AUDIO_SM], audio_buf[0], BUFFER_SAMPLES, false);

    cfg = dma_channel_get_default_config(dma_ch_b);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(AUDIO_PIO, AUDIO_SM, true));
    channel_config_set_chain_to(&cfg, dma_ch_a);
    dma_channel_configure(dma_ch_b, &cfg,
        &AUDIO_PIO->txf[AUDIO_SM], audio_buf[1], BUFFER_SAMPLES, false);

    dma_irqn_set_channel_enabled(1, dma_ch_a, true);
    dma_irqn_set_channel_enabled(1, dma_ch_b, true);
    irq_set_exclusive_handler(DMA_IRQ_1, audio_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    pio_sm_set_enabled(AUDIO_PIO, AUDIO_SM, true);
    dma_channel_start(dma_ch_a);
    return true;
}

void arch_i2s_set_fill_callback(hal_audio_fill_cb cb) {
    g_fill_cb = cb;
}

uint32_t arch_i2s_enter_critical(void) {
    return save_and_disable_interrupts();
}

void arch_i2s_exit_critical(uint32_t saved_state) {
    restore_interrupts(saved_state);
}

#endif // RP2
