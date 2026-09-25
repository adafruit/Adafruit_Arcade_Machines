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
    pinMode(FEATHER_TOUCH_CS_IRQ, INPUT_PULLUP); // V1 CS deasserted / V2 IRQ left alone
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

// --- Writing -----------------------------------------------------------------

hal_file_t *hal_storage_create(const char *path) {
    if (!s_mounted) return NULL;
    hal_file_t *slot = NULL;
    for (int i = 0; i < MAX_OPEN_FILES; i++)
        if (!file_pool[i].in_use) { slot = &file_pool[i]; break; }
    if (!slot) return NULL;
    if (!slot->fil.open(path, O_WRONLY | O_CREAT | O_TRUNC)) return NULL;
    slot->in_use = true;
    return slot;
}

uint32_t hal_storage_write(hal_file_t *f, const void *buf, uint32_t len) {
    if (!f) return 0;
    const size_t n = f->fil.write((const uint8_t *)buf, (size_t)len);
    return (uint32_t)n;
}

bool hal_storage_remove(const char *path) {
    if (!s_mounted) return false;
    if (!s_sd.exists(path)) return true;
    return s_sd.remove(path);
}

bool hal_storage_rename(const char *from, const char *to) {
    if (!s_mounted) return false;
    if (s_sd.exists(to)) return false; // FAT can't rename over a file
    return s_sd.rename(from, to);
}

// --- Contiguous files, written one sector at a time: NOT YET SUPPORTED ------
//
// On this board the SD card shares its SPI bus with the TFT, so a multi-
// sector write spread across frames would interleave with the display's own
// transfers. That needs its own design (extras/CONSOLES_PLAN.md, Phase 4).
// Until then these refuse, so a console machine keeps running and simply
// doesn't persist its save, rather than trying something half-working.
bool hal_storage_make_contiguous(const char *path, uint32_t size,
                                 const uint8_t *content, hal_storage_extent_t *out) {
    (void)path; (void)size; (void)content; (void)out;
    return false;
}
hal_storage_result_t hal_storage_extent_write_begin(const hal_storage_extent_t *e) { (void)e; return HAL_STORAGE_ERROR; }
hal_storage_result_t hal_storage_extent_write_sector(const uint8_t *data) { (void)data; return HAL_STORAGE_ERROR; }
hal_storage_result_t hal_storage_extent_write_end(void) { return HAL_STORAGE_ERROR; }

#endif
