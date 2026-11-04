/**
 * adcs_persist.c
 *
 * Saves the ADCS state that can't be recovered from the sensors (the TLE and UTC) to MRAM. Each record takes up one
 * MRAM page, and is protected by the driver's majority vote across the three chips plus a CRC. Attitude and filter
 * state aren't saved: they go stale within seconds, and the filter re-initializes from detumbling.
 *
 * Created: October 30, 2026
 * Authors: Zach Mahan
 */

#include "tasks/adcs/adcs_persist.h"

#include <string.h>

#include "drivers/mram/mram_driver.h"
#include "logging.h"
#include "tasks/adcs/adcs_task.h"

#define ADCS_MRAM_PAGE_SIZE 256 // mram_read_bytes() only reads whole pages
#define ADCS_MRAM_CHIP_COUNT 3
#define ADCS_MRAM_MIN_WORKING_CHIPS 2

// Changing the layout of a record means changing its magic, so that old records are rejected
#define ADCS_TLE_RECORD_MAGIC 0x41544C31u // "ATL1"
#define ADCS_UTC_RECORD_MAGIC 0x41555431u // "AUT1"

typedef struct {
    uint32_t magic;
    uint32_t size; // bytes of payload following this header
    uint32_t crc;  // over the rest of the header and the payload, with this field zeroed
} adcs_record_header_t;

static bool mram_usable = false;
static uint8_t page[ADCS_MRAM_PAGE_SIZE]; // only used by the ADCS task

/* ---------- RECORDS ---------- */

static uint32_t record_crc(const uint32_t size) {
    return crc32(page, sizeof(adcs_record_header_t) + size);
}

static void save_record(const uint32_t address, const uint32_t magic, const void *const payload, const uint32_t size) {
    if (!mram_usable) {
        return;
    }

    memset(page, 0, sizeof(page));
    adcs_record_header_t header = {.magic = magic, .size = size, .crc = 0};
    memcpy(page, &header, sizeof(header));
    memcpy(page + sizeof(header), payload, size);

    header.crc = record_crc(size);
    memcpy(page, &header, sizeof(header));

    mram_write_bytes(address, page, sizeof(page));
}

static bool load_record(const uint32_t address, const uint32_t magic, void *const payload, const uint32_t size) {
    if (!mram_usable) {
        return false;
    }

    // mram_read_bytes() can't report failure and leaves the buffer as it was, so an all-zero page must fail below
    memset(page, 0, sizeof(page));
    mram_read_bytes(address, page, sizeof(page));

    adcs_record_header_t header;
    memcpy(&header, page, sizeof(header));
    if (header.magic != magic || header.size != size) {
        return false;
    }

    const uint32_t saved_crc = header.crc;
    header.crc = 0;
    memcpy(page, &header, sizeof(header));
    if (record_crc(size) != saved_crc) {
        warning("adcs: MRAM record at 0x%lx failed its CRC\n", (unsigned long)address);
        return false;
    }

    memcpy(payload, page + sizeof(header), size);
    return true;
}

/* ---------- PUBLIC FUNCTIONS ---------- */

bool adcs_persist_init(void) {
    const mram_status_t status = mram_init();

    uint8_t working_chips = 0;
    for (uint8_t mram = 1; mram <= ADCS_MRAM_CHIP_COUNT; mram++) {
        if (get_mram_state(status, mram) == MRAM_STATE_OK) {
            working_chips++;
        }
    }

    mram_usable = working_chips >= ADCS_MRAM_MIN_WORKING_CHIPS;
    return mram_usable;
}

void adcs_persist_save_tle(const adcs_tle_t *const tle) {
    save_record(MRAM_ADCS_TLE_ADDRESS, ADCS_TLE_RECORD_MAGIC, tle, sizeof(*tle));
}

bool adcs_persist_load_tle(adcs_tle_t *const tle) {
    return load_record(MRAM_ADCS_TLE_ADDRESS, ADCS_TLE_RECORD_MAGIC, tle, sizeof(*tle));
}

void adcs_persist_save_utc(const uint32_t unix_time) {
    save_record(MRAM_ADCS_UTC_ADDRESS, ADCS_UTC_RECORD_MAGIC, &unix_time, sizeof(unix_time));
}

bool adcs_persist_load_utc(uint32_t *const unix_time) {
    return load_record(MRAM_ADCS_UTC_ADDRESS, ADCS_UTC_RECORD_MAGIC, unix_time, sizeof(*unix_time));
}
