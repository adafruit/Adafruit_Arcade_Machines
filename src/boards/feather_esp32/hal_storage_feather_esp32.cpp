// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// hal_storage.h for the Feather ESP32 V2 -- the microSD slot on the 2.4"
// TFT FeatherWing, via SdFat.
//
// Near-identical to the Fruit Jam backend, with one real difference:
// SHARED_SPI rather than DEDICATED_SPI, because the card sits on the same
// bus as the display. That costs nothing here -- storage is touched once at
// boot, before a single scanline is pushed, so the two never contend.
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32_V2)
#include <SdFat_Adafruit_Fork.h>
#include "hal/arcade_hal_storage.h"
#include "board_config_feather_esp32.h"

static SdFat s_sd;
static bool  s_mounted = false;

bool hal_storage_mount(void) {
    if (s_mounted) return true;
    pinMode(FEATHER_STMPE_CS, OUTPUT); digitalWrite(FEATHER_STMPE_CS, HIGH);
    s_mounted = s_sd.begin(SdSpiConfig(FEATHER_SD_CS, SHARED_SPI,
                                       SD_SCK_MHZ(16), &SPI));
    return s_mounted;
}

// DELIBERATELY does not call s_sd.end(), unlike the Fruit Jam backend.
//
// SdFat::end() tears down the SPI bus, and on this board the SD card SHARES
// that bus with the display. The Fruit Jam gets away with it because its
// card is on SPI0 while DVI runs on HSTX -- here, unmounting killed the
// panel. Found the hard way: the first frame after a successful asset load
// panicked with LoadProhibited inside the ESP32 SPI driver, because
// SPIClass's internal spi_t* had been freed out from under the display.
//
// Dropping the flag is the whole job. "Unmount" here means "this library is
// finished with storage", which is true -- the card is never touched again
// after boot -- and leaving the bus up is what the display needs.
void hal_storage_unmount(void) {
    s_mounted = false;
}

bool hal_storage_list_dir(const char *dir, hal_storage_dirent_cb cb, void *ctx) {
    if (!s_mounted) return false;
    File32 d;
    if (!d.open(dir, O_RDONLY)) return false;
    char name[256];
    File32 entry;
    while (entry.openNext(&d, O_RDONLY)) {
        if (!entry.isDir() && entry.getName(name, sizeof name)) cb(name, ctx);
        entry.close();
    }
    d.close();
    return true;
}

struct hal_file { File32 fil; bool in_use; };
#define MAX_OPEN_FILES 2
static hal_file_t file_pool[MAX_OPEN_FILES];

hal_file_t *hal_storage_open(const char *path) {
    if (!s_mounted) return NULL;
    hal_file_t *slot = NULL;
    for (int i = 0; i < MAX_OPEN_FILES; i++)
        if (!file_pool[i].in_use) { slot = &file_pool[i]; break; }
    if (!slot) return NULL;
    if (!slot->fil.open(path, O_RDONLY)) return NULL;
    slot->in_use = true;
    return slot;
}

uint32_t hal_storage_read(hal_file_t *f, void *buf, uint32_t len) {
    if (!f) return 0;
    int br = f->fil.read(buf, (size_t)len);
    return (br > 0) ? (uint32_t)br : 0u;
}

void hal_storage_close(hal_file_t *f) {
    if (!f) return;
    f->fil.close();
    f->in_use = false;
}
#endif
