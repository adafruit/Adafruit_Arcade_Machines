// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// hal_audio.h implementation for the Adafruit Fruit Jam.
//
// Two halves, now living in the two places they belong:
//
//   this file        the DAC. Which chip (TLV320DAC3100), where it sits on
//                    I2C, which pins, how it is configured -- all board
//                    facts, now expressed through Adafruit_TLV320_I2S
//                    instead of ~45 hand-written register pokes.
//   src/arch/rp2040/ the transport. A PIO state machine and a DMA pair
//                    pushing samples at the DAC. That is silicon, shared by
//                    every RP2 board, and it stays hand-written because no
//                    library covers it -- Adafruit_TLV320_I2S is I2C
//                    configuration only, with no data path.
//
// THE CONFIGURATION IS A TRANSLATION, NOT A REDESIGN. Every step below is
// the same operation the register sequence did, in the same order, checked
// against the library's own basicI2Sconfig example. Two things the old
// sequence did are deliberately NOT carried over:
//
//   - NADC/MADC (registers 0x12/0x13) and the ADC block (0x51-0x53). The
//     library has no setter for either, and Adafruit's own reference init
//     does not touch them -- this part's ADC is not in the audio path here.
//     They appear to have been inherited from a fuller reference driver.
//   - The explicit i2c_init()/gpio_set_function() calls. Wire does that,
//     and the library takes a TwoWire.
//
// If audio ever comes back wrong after this change, those two omissions are
// the first place to look.
// Whole file guarded on the BOARD, not the architecture. Arduino compiles
// every source under src/ regardless of the selected board, so a second
// backend would otherwise collide with this one on all 21 HAL functions.
// The board macro rather than ARDUINO_ARCH_RP2040 because a Feather RP2350
// would share the arch and still need its own backend. See PORTING.md.
#if defined(ARDUINO_ADAFRUIT_FRUITJAM_RP2350)

#include <Adafruit_TLV320DAC3100.h>
#include <Wire.h>

#include "hal/arcade_hal_audio.h"
#include "board_config_fruitjam.h"
#include "arch/rp2040/arch_audio_i2s.h"

static Adafruit_TLV320DAC3100 s_codec;

// GPIO 22 resets the DAC *and* the onboard ESP32-C6, so it is held high
// rather than pulsed -- dropping it would reset the radio too.
static bool codec_init(void) {
    pinMode(FRUITJAM_CODEC_RESET_PIN, OUTPUT);
    digitalWrite(FRUITJAM_CODEC_RESET_PIN, HIGH);
    delay(100);

    Wire.setSDA(FRUITJAM_I2C_SDA_PIN);
    Wire.setSCL(FRUITJAM_I2C_SCL_PIN);
    Wire.begin();

    if (!s_codec.begin(FRUITJAM_DAC_I2C_ADDR, &Wire)) return false;
    delay(10);

    // I2S, 16-bit. The RP2 side is the clock master, so no BCLK/WCLK out.
    if (!s_codec.setCodecInterface(TLV320DAC3100_FORMAT_I2S,
                                   TLV320DAC3100_DATA_LEN_16)) return false;

    // The DAC's PLL derives everything from BCLK -- there is no separate
    // MCLK line on this board.
    if (!s_codec.setCodecClockInput(TLV320DAC3100_CODEC_CLKIN_PLL) ||
        !s_codec.setPLLClockInput(TLV320DAC3100_PLL_CLKIN_BCLK)) return false;

    // P=1, R=2, J=32, D=0 -- the same values the register sequence wrote to
    // 0x05-0x08, and the same the library's example uses.
    if (!s_codec.setPLLValues(1, 2, 32, 0)) return false;
    if (!s_codec.setNDAC(true, 8) || !s_codec.setMDAC(true, 2)) return false;
    if (!s_codec.powerPLL(true)) return false;

    if (!s_codec.setDACDataPath(true, true,
                                TLV320_DAC_PATH_NORMAL,
                                TLV320_DAC_PATH_NORMAL,
                                TLV320_VOLUME_STEP_1SAMPLE)) return false;

    // Both DACs into the output mixer; no analogue inputs routed.
    if (!s_codec.configureAnalogInputs(TLV320_DAC_ROUTE_MIXER,
                                       TLV320_DAC_ROUTE_MIXER,
                                       false, false, false, false)) return false;

    // Unmute, 0 dB. The old sequence wrote 0x00 to both channel volume
    // registers, which is this part's 0 dB code point.
    if (!s_codec.setDACVolumeControl(false, false, TLV320_VOL_INDEPENDENT) ||
        !s_codec.setChannelVolume(false, 0) ||
        !s_codec.setChannelVolume(true, 0)) return false;

    // Headphone drivers, then the speaker amp. Gains match the old writes
    // to page 1 0x24/0x25 (headphone) and 0x26 (speaker).
    if (!s_codec.configureHeadphoneDriver(true, true) ||
        !s_codec.configureHPL_PGA(0, true) ||
        !s_codec.configureHPR_PGA(0, true) ||
        !s_codec.setHPLVolume(true, 0x0A) ||
        !s_codec.setHPRVolume(true, 0x0A)) return false;

    if (!s_codec.enableSpeaker(true) ||
        !s_codec.configureSPK_PGA(TLV320_SPK_GAIN_6DB, true) ||
        !s_codec.setSPKVolume(true, 0x0A)) return false;

    // Headset detect, so plugging headphones in mutes the speaker.
    if (!s_codec.setHeadsetDetect(true)) return false;

    return true;
}

bool hal_audio_init(uint32_t sample_rate) {
    // A codec that fails to configure is reported rather than swallowed --
    // the old code returned true unconditionally, so a dead DAC looked
    // exactly like a silent game.
    bool codec_ok = codec_init();
    bool i2s_ok = arch_i2s_init(sample_rate,
                                FRUITJAM_I2S_DIN_PIN, FRUITJAM_I2S_BCLK_PIN);
    return codec_ok && i2s_ok;
}

void hal_audio_set_fill_callback(hal_audio_fill_cb cb) {
    arch_i2s_set_fill_callback(cb);
}

uint32_t hal_audio_enter_critical(void) {
    return arch_i2s_enter_critical();
}

void hal_audio_exit_critical(uint32_t saved_state) {
    arch_i2s_exit_critical(saved_state);
}

#endif // ARDUINO_ADAFRUIT_FRUITJAM_RP2350
