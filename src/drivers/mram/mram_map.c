/**
 * mram_map.c
 *
 * PVDXos's MRAM regions, region-checked access to them, a mutex so tasks don't interleave SPI transactions, and
 * records that fall back to another chip when one chip's copy is bad. See mram_map.h for the layout.
 *
 * Created: November 7, 2026
 * Authors: PVDX
 */

#include "mram_map.h"

#include <FreeRTOS.h>
#include <semphr.h>
#include <string.h>
#include <task.h>

#include "image_format.h"
#include "logging.h"
#include "peripheral_clk_config.h" // CONF_GCLK_SERCOM0_CORE_FREQUENCY

#define MRAM_CHIP_COUNT 3u
#define MRAM_PAGE_BYTES 256u // The driver's voting unit: mram_read_bytes() sizes must be a multiple of it

_Static_assert(MRAM_OS_ADDR + MRAM_OS_SIZE <= MRAM_OS_START, "PVDXos regions must start after the golden copies");
_Static_assert(MRAM_BOOTLOADER_ADDR + MRAM_BOOTLOADER_SIZE <= MRAM_OS_START, "PVDXos regions must start after the golden copies");
_Static_assert(MRAM_BURN_RECORD_ADDRESS >= MRAM_OS_START, "MRAM regions overlap");
_Static_assert(MRAM_ADCS_ADDRESS >= MRAM_BURN_RECORD_ADDRESS + MRAM_BURN_RECORD_SIZE, "MRAM regions overlap");
_Static_assert(MRAM_PEROVSKITE_ADDRESS >= MRAM_ADCS_ADDRESS + MRAM_ADCS_SIZE, "MRAM regions overlap");
_Static_assert(MRAM_CAMERA_ADDRESS >= MRAM_PEROVSKITE_ADDRESS + MRAM_PEROVSKITE_SIZE, "MRAM regions overlap");
_Static_assert(MRAM_CAMERA_ADDRESS + MRAM_CAMERA_SIZE <= MRAM_CHIP_SIZE, "MRAM regions must fit in the chip");

static const struct {
    uint32_t address;
    uint32_t size;
} regions[MRAM_REGION_COUNT] = {
    [MRAM_REGION_BURN_RECORD] = {MRAM_BURN_RECORD_ADDRESS, MRAM_BURN_RECORD_SIZE},
    [MRAM_REGION_ADCS] = {MRAM_ADCS_ADDRESS, MRAM_ADCS_SIZE},
    [MRAM_REGION_PEROVSKITE] = {MRAM_PEROVSKITE_ADDRESS, MRAM_PEROVSKITE_SIZE},
    [MRAM_REGION_CAMERA] = {MRAM_CAMERA_ADDRESS, MRAM_CAMERA_SIZE},
};

typedef struct {
    uint32_t size; // Bytes of payload following this header
    uint32_t crc;  // crc32() of the payload
} mram_record_header_t;

_Static_assert(sizeof(mram_record_header_t) == MRAM_RECORD_OVERHEAD, "MRAM_RECORD_OVERHEAD must match the record header");

/* ---------- LOCKING ---------- */

static StaticSemaphore_t mutex_storage;
static SemaphoreHandle_t mutex = NULL;

// Returns whether it took the mutex. Before the scheduler starts there is only one thread, so there is nothing to
// serialize. Must not be called with the scheduler suspended.
static bool lock(void) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return false;
    }
    if (mutex == NULL) {
        vTaskSuspendAll();
        if (mutex == NULL) {
            mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
        }
        (void)xTaskResumeAll();
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    return true;
}

static void unlock(bool locked) {
    if (locked) {
        xSemaphoreGive(mutex);
    }
}

/* ---------- REGIONS ---------- */

// Finds the MRAM address of `size` bytes at `offset` in `region`, if they fit there
static bool region_address(mram_region_t region, uint32_t offset, uint32_t size, uint32_t *p_address) {
    if (region >= MRAM_REGION_COUNT || offset > regions[region].size || size > regions[region].size - offset) {
        return false;
    }
    *p_address = regions[region].address + offset;
    return true;
}

static mram_status_t init_status = 0;

// SPI clock for all three chips in PVDXos. The bootloader keeps its ASF configuration.
#define MRAM_SPI_HZ 1000000

mram_status_t mram_map_init(void) {
    // spi_m_sync_set_baudrate() takes the BAUD register value, not a frequency, and only while the SPI is disabled;
    // mram_init() enables it.
    spi_m_sync_disable(&SPI_MRAM);
    spi_m_sync_set_baudrate(&SPI_MRAM, CONF_GCLK_SERCOM0_CORE_FREQUENCY / (2 * MRAM_SPI_HZ) - 1);
    init_status = mram_init();
    return init_status;
}

// The driver votes over whole 256-byte pages, so read any size as whole pages plus, for the remainder, the last
// 256 bytes ending at `address + size` (all PVDXos regions start well past address 256). Returns true on success.
// Callers hold the lock.
static bool read_any_size(uint32_t address, uint8_t *data, uint32_t size) {
    static uint8_t tail[MRAM_PAGE_BYTES];
    uint32_t whole = size - size % MRAM_PAGE_BYTES;
    if (whole > 0u && mram_read_bytes(address, data, whole, NULL)) {
        return false;
    }
    uint32_t rest = size - whole;
    if (rest == 0u) {
        return true;
    }
    if (mram_read_bytes(address + size - MRAM_PAGE_BYTES, tail, MRAM_PAGE_BYTES, NULL)) {
        return false;
    }
    memcpy(data + whole, tail + MRAM_PAGE_BYTES - rest, rest);
    return true;
}

mram_status_t mram_map_status(void) {
    return init_status;
}

bool mram_region_read(mram_region_t region, uint32_t offset, void *data, uint32_t size) {
    uint32_t address;
    if (!region_address(region, offset, size, &address)) {
        return false;
    }
    bool locked = lock();
    bool success = read_any_size(address, data, size);
    unlock(locked);
    return success;
}

bool mram_region_write(mram_region_t region, uint32_t offset, const void *data, uint32_t size) {
    uint32_t address;
    if (!region_address(region, offset, size, &address)) {
        return false;
    }
    bool locked = lock();
    bool success = !mram_write_bytes(address, data, size);
    unlock(locked);
    return success;
}

bool mram_bootloader_copy_read(uint32_t offset, void *data, uint32_t size) {
    if (offset > MRAM_BOOTLOADER_SIZE || size > MRAM_BOOTLOADER_SIZE - offset) {
        return false;
    }
    bool locked = lock();
    bool success = !mram_read_bytes(MRAM_BOOTLOADER_ADDR + offset, data, size, NULL);
    unlock(locked);
    return success;
}

/* ---------- RECORDS ---------- */

static bool record_address(mram_region_t region, uint32_t offset, uint32_t size, uint32_t *p_address) {
    return size <= MRAM_CHIP_SIZE && region_address(region, offset, MRAM_RECORD_OVERHEAD + size, p_address);
}

static bool write_record_to_chip(uint8_t mram, uint32_t address, const mram_record_header_t *p_header, const void *payload) {
    return !mram_write_module(mram, address, (const uint8_t *)p_header, sizeof(*p_header)) &&
        !mram_write_module(mram, address + sizeof(*p_header), payload, p_header->size);
}

bool mram_record_save(mram_region_t region, uint32_t offset, const void *payload, uint32_t size) {
    uint32_t address;
    if (!record_address(region, offset, size, &address)) {
        return false;
    }
    const mram_record_header_t header = {.size = size, .crc = crc32(payload, size)};

    bool locked = lock();
    bool all_okay = true;
    for (uint8_t mram = 1; mram <= MRAM_CHIP_COUNT; mram++) {
        all_okay = write_record_to_chip(mram, address, &header, payload) && all_okay;
    }
    unlock(locked);
    return all_okay;
}

bool mram_record_load(mram_region_t region, uint32_t offset, void *payload, uint32_t size) {
    uint32_t address;
    if (!record_address(region, offset, size, &address)) {
        return false;
    }

    bool locked = lock();
    mram_record_header_t header;
    uint8_t good_chip = 0;
    for (uint8_t mram = 1; mram <= MRAM_CHIP_COUNT && good_chip == 0; mram++) {
        if (!mram_read_module(mram, address, (uint8_t *)&header, sizeof(header)) && header.size == size &&
            !mram_read_module(mram, address + sizeof(header), payload, size) && crc32(payload, size) == header.crc) {
            good_chip = mram;
        }
    }

    if (good_chip > 1) {
        warning("mram: record at 0x%lx is bad on chip(s) before MRAM%u\n", (unsigned long)address, good_chip);
    }
    if (good_chip != 0) {
        for (uint8_t mram = 1; mram <= MRAM_CHIP_COUNT; mram++) {
            if (mram != good_chip) {
                (void)write_record_to_chip(mram, address, &header, payload);
            }
        }
    }
    unlock(locked);
    return good_chip != 0;
}

bool mram_record_payload_offset(mram_region_t region, uint32_t offset, uint32_t size, uint32_t *p_payload_offset) {
    uint32_t address;
    mram_record_header_t header;
    if (!record_address(region, offset, size, &address) || !mram_region_read(region, offset, &header, sizeof(header)) ||
        header.size != size) {
        return false;
    }
    *p_payload_offset = offset + sizeof(header);
    return true;
}
