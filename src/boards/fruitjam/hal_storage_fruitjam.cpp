// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// hal_storage.h implementation for the Adafruit Fruit Jam, on SdFat.
//
// This used to be a hand-rolled SPI SD driver (sdcard.c) plus a vendored
// copy of FatFs -- about 24,000 lines carried in this repository to do what
// a maintained library already does. Both are gone; SdFat is declared in
// library.properties and Library Manager installs it.
//
// Nothing above this file changed: Machine code still sees only the six
// functions in arcade_hal_storage.h, and still touches storage exactly once,
// at boot, before the game starts.
//
// Two details preserved deliberately from the old driver:
//
//  - 12.5 MHz SPI. sdcard.c settled on SD_SPI_FAST_HZ = 12500000 after
//    init, and that is a number this project has actually run cards at
//    rather than a default worth re-guessing. SdFat handles the slow
//    (400 kHz) init handshake itself.
//  - Long filenames. The old ffconf.h set FF_USE_LFN 1 / FF_MAX_LFN 255, so
//    hal_storage_list_dir() reported long names, and the ROM loaders sort
//    the names they are given (see invaders_assets.cpp). SdFat's getName()
//    is also long-name, so the sort order is unchanged. That LFN setting,
//    with code page 437, is why the vendored FatFs needed a 15,597-line
//    ffunicode.c.
//
// The card-detect pin (GPIO 33) is not used. The old driver configured it
// with a pull-up and then never read it; a missing card already surfaces as
// a failed mount, which is what the boot-error screen keys off.
// Whole file guarded on the BOARD, not the architecture. Arduino compiles
// every source under src/ regardless of the selected board, so a second
// backend would otherwise collide with this one on all 21 HAL functions.
// The board macro rather than ARDUINO_ARCH_RP2040 because a Feather RP2350
// would share the arch and still need its own backend. See PORTING.md.
#if defined(ARDUINO_ADAFRUIT_FRUITJAM_RP2350)

#include <SdFat_Adafruit_Fork.h>

#include <string.h>
#include "hal/arcade_hal_storage.h"

// PIN_SD_* come from the arduino-pico Fruit Jam variant, and are the same
// pins as its default SPI0 (PIN_SPI0_SCK/MOSI/MISO/SS), so the stock `SPI`
// object drives the card with no remapping. Using the variant's own names
// rather than private defines keeps this file honest about where the wiring
// facts live.
#define SD_SPI_HZ 12500000UL

static SdFat s_sd;
static bool  s_mounted = false;

// A CARD LEFT MID-READ BY A RESET. With DEDICATED_SPI, SdFat keeps the card
// in a multi-block read between calls; a reset that isn't a power-off
// (a crash reboot, the watchdog, a BOOT-button replug while the board is
// powered from elsewhere) leaves it still in that read, and it ignores
// sd.begin()'s CMD0 -- RED, until the card loses power. So before
// mounting: clock out the rest of any block it is sending, then CMD12
// (STOP_TRANSMISSION) and wait for it to finish. A card at rest answers
// CMD12 with "illegal command", which is harmless.
static void sd_unstick(void) {
    pinMode(PIN_SD_DAT3_CS, OUTPUT);
    digitalWrite(PIN_SD_DAT3_CS, HIGH);
    SPI.begin();
    SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_SD_DAT3_CS, LOW);
    for (int i = 0; i < 1100; i++) SPI.transfer(0xFF);    // a block + CRC + gap
    static const uint8_t kCmd12[6] = { 0x4C, 0, 0, 0, 0, 0x61 };
    for (uint8_t b : kCmd12) SPI.transfer(b);
    SPI.transfer(0xFF);                                    // the stuff byte
    for (int i = 0; i < 16 && (SPI.transfer(0xFF) & 0x80); i++) {}  // R1
    for (int i = 0; i < 20000 && SPI.transfer(0xFF) != 0xFF; i++) {} // busy
    digitalWrite(PIN_SD_DAT3_CS, HIGH);
    for (int i = 0; i < 16; i++) SPI.transfer(0xFF);
    SPI.endTransaction();
    SPI.end();
}

bool hal_storage_mount(void) {
    if (s_mounted) return true;

    // Must precede SPI.begin(), which SdFat calls from sd.begin(). These
    // match the variant's SPI0 defaults; setting them explicitly documents
    // which bus the card is on.
    SPI.setSCK(PIN_SD_CLK);
    SPI.setTX(PIN_SD_CMD_MOSI);
    SPI.setRX(PIN_SD_DAT0_MISO);
    SPI.setCS(PIN_SD_DAT3_CS);
    sd_unstick();

    // DEDICATED_SPI: nothing else lives on SPI0 on this board (the variant
    // puts SPIWIFI on SPI1), so SdFat may keep the bus configured.
    s_mounted = s_sd.begin(SdSpiConfig(PIN_SD_DAT3_CS, DEDICATED_SPI,
                                       SD_SPI_HZ, &SPI));
    return s_mounted;
}

void hal_storage_unmount(void) {
    if (!s_mounted) return;
    s_sd.end();
    s_mounted = false;
}

bool hal_storage_list_dir(const char *dir, hal_storage_dirent_cb cb, void *ctx) {
    if (!s_mounted) return false;

    File32 d;
    if (!d.open(dir, O_RDONLY)) return false;

    // FF_MAX_LFN was 255; match it so a long name is never silently
    // truncated into a different sort position.
    char name[256];
    File32 entry;
    while (entry.openNext(&d, O_RDONLY)) {
        if (!entry.isDir() && entry.getName(name, sizeof name)) {
            cb(name, ctx);
        }
        entry.close();
    }
    d.close();
    return true;
}

// Sequential open/read/close only -- nothing in this codebase opens more
// than one file at a time (ROM chips and WAV samples are each loaded one
// file at a time). A tiny fixed pool keeps hal_file_t opaque without
// dynamic allocation, unchanged from the FatFs implementation.
struct hal_file {
    File32 fil;
    bool   in_use;
};

// 4: the SCUMM engine keeps a room file open, and opens sound and save
// files beside it. Each File32 is a few dozen bytes; the cache is shared.
#define MAX_OPEN_FILES 4
static hal_file_t file_pool[MAX_OPEN_FILES];

hal_file_t *hal_storage_open(const char *path) {
    if (!s_mounted) return NULL;

    hal_file_t *slot = NULL;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!file_pool[i].in_use) { slot = &file_pool[i]; break; }
    }
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

bool hal_storage_seek(hal_file_t *f, uint32_t pos) {
    return f && f->fil.seekSet(pos);
}

uint32_t hal_storage_size(hal_file_t *f) {
    return f ? (uint32_t)f->fil.fileSize() : 0u;
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

// --- Contiguous files, written one sector at a time without blocking --------

bool hal_storage_make_contiguous(const char *path, uint32_t size,
                                 const uint8_t *content, hal_storage_extent_t *out) {
    if (!s_mounted || size == 0) return false;
    memset(out, 0, sizeof *out);
    File32 f;
    if (s_sd.exists(path)) {
        uint32_t bgn = 0, end = 0;
        bool reuse = false;
        if (f.open(path, O_RDONLY)) {
            reuse = f.fileSize() >= size && f.contiguousRange(&bgn, &end);
            f.close();
        }
        if (reuse) {
            out->first_sector = bgn;
            out->sectors = (size + HAL_STORAGE_SECTOR - 1u) / HAL_STORAGE_SECTOR;
            strncpy(out->path, path, sizeof out->path - 1);
            return true;
        }
        if (!s_sd.remove(path)) return false;
    }
    // preAllocate() claims one contiguous run and sets the size; the data in
    // it is whatever was on the card, so `content` is written over it.
    if (!f.open(path, O_RDWR | O_CREAT | O_TRUNC)) return false;
    bool ok = f.preAllocate(size) && f.write(content, size) == size;
    f.close();
    uint32_t bgn = 0, end = 0;
    ok = ok && f.open(path, O_RDONLY) && f.contiguousRange(&bgn, &end);
    if (f.isOpen()) f.close();
    if (!ok) return false;
    out->first_sector = bgn;
    out->sectors = (size + HAL_STORAGE_SECTOR - 1u) / HAL_STORAGE_SECTOR;
    strncpy(out->path, path, sizeof out->path - 1);
    return true;
}

// The card is always SPI on this board, so SdFat's generic card pointer is
// its SPI card, whose streaming calls are what make this non-blocking:
// writeData() and writeStop() each wait for the card only at their START,
// which returns at once when isBusy() has just said the card is free.
// (writeSector() is no good: it also waits for the flash to be programmed.)
static SdSpiCard *spi_card(void) { return static_cast<SdSpiCard *>(s_sd.card()); }

hal_storage_result_t hal_storage_extent_write_begin(const hal_storage_extent_t *e) {
    if (!s_mounted || !e || e->sectors == 0) return HAL_STORAGE_ERROR;
    SdSpiCard *c = spi_card();
    if (c->isBusy()) return HAL_STORAGE_BUSY;
    // In dedicated-SPI mode SdFat can leave the card partway through a
    // multi-sector READ; close that before starting a write.
    if (!c->syncDevice()) return HAL_STORAGE_ERROR;
    if (c->isBusy()) return HAL_STORAGE_BUSY;
    return c->writeStart(e->first_sector) ? HAL_STORAGE_OK : HAL_STORAGE_ERROR;
}

hal_storage_result_t hal_storage_extent_write_sector(const uint8_t *data) {
    SdSpiCard *c = spi_card();
    if (c->isBusy()) return HAL_STORAGE_BUSY;
    return c->writeData(data) ? HAL_STORAGE_OK : HAL_STORAGE_ERROR;
}

hal_storage_result_t hal_storage_extent_write_end(void) {
    SdSpiCard *c = spi_card();
    if (c->isBusy()) return HAL_STORAGE_BUSY;
    return c->writeStop() ? HAL_STORAGE_OK : HAL_STORAGE_ERROR;
}

#endif // ARDUINO_ADAFRUIT_FRUITJAM_RP2350
