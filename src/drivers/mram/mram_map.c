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
#include <task.h>

#include "../../../bootloader/src/image_format.h"
#include "logging.h"

#define MRAM_CHIP_COUNT 3u

_Static_assert(MRAM_GOLDEN_APP_ADDR + MRAM_GOLDEN_APP_MAX_SIZE <= MRAM_OS_START, "PVDXos regions must start after the golden copy");
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

mram_status_t mram_map_init(void) {
    bool locked = lock();
    mram_status_t status = mram_init();
    unlock(locked);
    return status;
}

bool mram_region_read(mram_region_t region, uint32_t offset, void *data, uint32_t size) {
    uint32_t address;
    if (!region_address(region, offset, size, &address)) {
        return false;
    }
    bool locked = lock();
    bool success = mram_read_bytes(address, data, size);
    unlock(locked);
    return success;
}

bool mram_region_write(mram_region_t region, uint32_t offset, const void *data, uint32_t size) {
    uint32_t address;
    if (!region_address(region, offset, size, &address)) {
        return false;
    }
    bool locked = lock();
    bool success = mram_write_bytes(address, data, size);
    unlock(locked);
    return success;
}

/* ---------- RECORDS ---------- */

static bool record_address(mram_region_t region, uint32_t offset, uint32_t size, uint32_t *p_address) {
    return size <= MRAM_CHIP_SIZE && region_address(region, offset, MRAM_RECORD_OVERHEAD + size, p_address);
}

static bool write_record_to_chip(uint8_t mram, uint32_t address, const mram_record_header_t *p_header, const void *payload) {
    return mram_write_chip(mram, address, (const uint8_t *)p_header, sizeof(*p_header)) &&
        mram_write_chip(mram, address + sizeof(*p_header), payload, p_header->size);
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
        if (mram_read_chip(mram, address, (uint8_t *)&header, sizeof(header)) && header.size == size &&
            mram_read_chip(mram, address + sizeof(header), payload, size) && crc32(payload, size) == header.crc) {
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
