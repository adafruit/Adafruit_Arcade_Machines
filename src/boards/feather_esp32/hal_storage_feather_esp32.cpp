// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// hal_storage.h for the Feather ESP32 V2 -- the microSD slot on the 2.4"
// TFT FeatherWing, via SdFat.
//
// Near-identical to the Fruit Jam backend, with one real difference:
// SHARED_SPI rather than DEDICATED_SPI, because the card sits on the same
// bus as the display. At boot that costs nothing: SdFat reads the ROMs
// before a single scanline is pushed. Mid-game saves are the exception, and
// have their own path at the bottom of this file.
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32_V2)
#include <SdFat_Adafruit_Fork.h>
#include <string.h>
#include "hal/arcade_hal_storage.h"
#include "board_config_feather_esp32.h"
#include "hal_storage_feather_esp32.h"
#include "arch/esp32/arch_spi_dma.h"

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
// 2 here (the Fruit Jam has 4, for the SCUMM engine, which doesn't run on
// this board): each slot is a File32 in DRAM, and Galaga on the Feather
// has only ~100 bytes of DRAM to spare -- 4 overflowed it by 24 bytes.
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

// --- Contiguous files, written one sector at a time -------------------------
//
// THE BOOT HALF is the Fruit Jam's: SdFat still owns the bus, so the save
// file is made one contiguous run of sectors and its first sector noted.
// Whether the card addresses by block (SDHC/SDXC) or by byte (old SDSC) is
// noted too, because after the handover nothing here can ask SdFat.

static bool s_block_addressed = true;

bool hal_storage_make_contiguous(const char *path, uint32_t size,
                                 const uint8_t *content, hal_storage_extent_t *out) {
    if (!s_mounted || size == 0) return false;
    memset(out, 0, sizeof *out);
    s_block_addressed = s_sd.card()->type() == SD_CARD_TYPE_SDHC;
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

// THE MID-GAME HALF is different, because after hal_video_run() the IDF
// driver owns the bus, SdFat's SPIClass is gone, and the panel's chip select
// is held low for good, so it would take any other traffic as pixels
// (arch_spi_dma.h). So:
//
//   - The three calls below, made by console_save from the EMULATION core,
//     only put a request in a one-slot mailbox. They return BUSY while one
//     is waiting, which console_save already handles as "call again".
//   - feather_storage_service(), called by the video HAL at the end of each
//     painted frame on the PAINTING core, with the bus idle, carries the
//     request out: panel deselected, the card selected, one raw SD command
//     or one sector, the bus given back. The display and the card are
//     never on the bus together, because one core drives both, in turn.
//
// The card is still in SPI mode from SdFat's mount (it stays so until power
// is removed), and a multi-sector write (CMD25) is exactly the protocol
// SdFat's own writeStart/writeData/writeStop use on the Fruit Jam. One
// request per painted frame: an 8 KB save is 18 requests, ~0.6 s.
//
// begin() and write_sector() report OK once QUEUED; an error surfaces on
// the next call, and console_save then abandons that save and retries
// later, which is what it does on the Fruit Jam too. end() reports OK only
// once the stop has really been sent, so a finished save is a finished one.

enum : uint8_t { OP_NONE = 0, OP_BEGIN, OP_SECTOR, OP_END };

static volatile uint8_t  s_op = OP_NONE;       // the mailbox (emulation core writes)
static uint32_t          s_op_sector;          // OP_BEGIN: the extent's first sector
static const uint8_t    *s_op_data;            // OP_SECTOR: 512 bytes, left alone until done
static volatile bool     s_failed = false;     // a queued request failed
static volatile bool     s_end_done = false;   // OP_END has been carried out
static bool              s_in_write = false;   // the card is inside a CMD25 (service core)
static uint32_t          s_service_us_max = 0;

// One sector with its start token and CRC, in internal DRAM for the driver.
static uint8_t s_block[1 + HAL_STORAGE_SECTOR + 2] __attribute__((aligned(4)));

static void publish(uint8_t op) { __atomic_store_n(&s_op, op, __ATOMIC_RELEASE); }
static uint8_t pending(void) { return __atomic_load_n(&s_op, __ATOMIC_ACQUIRE); }

hal_storage_result_t hal_storage_extent_write_begin(const hal_storage_extent_t *e) {
    if (!e || e->sectors == 0) return HAL_STORAGE_ERROR;
    if (pending() != OP_NONE) return HAL_STORAGE_BUSY;
    s_failed = false;
    s_end_done = false;
    s_op_sector = e->first_sector;
    publish(OP_BEGIN);
    return HAL_STORAGE_OK;
}

hal_storage_result_t hal_storage_extent_write_sector(const uint8_t *data) {
    if (pending() != OP_NONE) return HAL_STORAGE_BUSY;
    if (s_failed) { s_failed = false; return HAL_STORAGE_ERROR; }
    s_op_data = data;
    publish(OP_SECTOR);
    return HAL_STORAGE_OK;
}

hal_storage_result_t hal_storage_extent_write_end(void) {
    if (pending() != OP_NONE) return HAL_STORAGE_BUSY;
    if (s_failed) { s_failed = false; return HAL_STORAGE_ERROR; }
    if (s_end_done) { s_end_done = false; return HAL_STORAGE_OK; }
    publish(OP_END);
    return HAL_STORAGE_BUSY;   // OK once it has actually been sent
}

// --- Raw SD, on the painting core with the bus idle -------------------------

// An SD card holds MISO low while it programs; 0xFF means ready.
static bool card_busy(void) { return arch_spi_aux_byte(0xFF) != 0xFF; }

// A command frame (CRC is off in SPI mode after init; any value but CMD0's
// is ignored), then R1 within eight bytes: bit 7 clear.
static uint8_t card_command(uint8_t cmd, uint32_t arg) {
    uint8_t f[6] = { (uint8_t)(0x40u | cmd), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
                     (uint8_t)(arg >> 8), (uint8_t)arg, 0x01 };
    for (int i = 0; i < 6; i++) (void)arch_spi_aux_byte(f[i]);
    uint8_t r1 = 0xFF;
    for (int i = 0; i < 8 && (r1 & 0x80); i++) r1 = arch_spi_aux_byte(0xFF);
    return r1;
}

static bool card_write_start(uint32_t sector) {
    const uint32_t arg = s_block_addressed ? sector : sector * HAL_STORAGE_SECTOR;
    return card_command(25, arg) == 0x00;          // CMD25: WRITE_MULTIPLE_BLOCK
}

static bool card_write_data(const uint8_t *data) {
    s_block[0] = 0xFC;                              // multi-block start token
    memcpy(s_block + 1, data, HAL_STORAGE_SECTOR);
    s_block[1 + HAL_STORAGE_SECTOR] = 0xFF;         // CRC, unchecked
    s_block[2 + HAL_STORAGE_SECTOR] = 0xFF;
    arch_spi_aux_xfer(s_block, NULL, sizeof s_block);
    return (arch_spi_aux_byte(0xFF) & 0x1F) == 0x05; // data response: accepted
}

static void card_write_stop(void) {
    (void)arch_spi_aux_byte(0xFD);                  // stop-transmission token
    (void)arch_spi_aux_byte(0xFF);                  // the card goes busy after this
}

void feather_storage_service(void) {
    const uint8_t op = pending();
    if (op == OP_NONE) return;
    const uint32_t t0 = micros();
    arch_spi_aux_select();
    if (!card_busy()) {
        bool ok = true;
        switch (op) {
        case OP_BEGIN:
            if (s_in_write) { card_write_stop(); s_in_write = false; } // an abandoned save's
            if (card_busy()) { arch_spi_aux_deselect(); return; }      // try again next frame
            ok = card_write_start(s_op_sector);
            s_in_write = ok;
            break;
        case OP_SECTOR:
            ok = s_in_write && card_write_data(s_op_data);
            if (!ok && s_in_write) { card_write_stop(); s_in_write = false; }
            break;
        case OP_END:
            if (s_in_write) card_write_stop();
            s_in_write = false;
            s_end_done = true;
            break;
        }
        if (!ok) s_failed = true;
        publish(OP_NONE);
    }
    arch_spi_aux_deselect();
    const uint32_t us = micros() - t0;
    if (us > s_service_us_max) s_service_us_max = us;
}

uint32_t feather_storage_take_service_us_max(void) {
    const uint32_t v = s_service_us_max;
    s_service_us_max = 0;
    return v;
}

#endif
