/**
 * adcs_persist.c
 *
 * Saves the ADCS state that can't be recovered from the sensors (the TLE and UTC) to MRAM, as records in the ADCS
 * region (see mram_map.h), each checked by a CRC and kept on all three chips. Attitude and filter state aren't saved:
 * they go stale within seconds, and the filter re-initializes from detumbling.
 *
 * Created: October 30, 2026
 * Authors: Zach Mahan
 */

#include "tasks/adcs/adcs_persist.h"

#include "drivers/mram/mram_map.h"
#include "tasks/adcs/adcs_task.h"

#define ADCS_MRAM_CHIP_COUNT 3
#define ADCS_MRAM_MIN_WORKING_CHIPS 2

// Offsets of the records in the ADCS MRAM region
#define ADCS_TLE_RECORD_OFFSET 0x000u
#define ADCS_UTC_RECORD_OFFSET 0x100u

_Static_assert(MRAM_RECORD_OVERHEAD + sizeof(adcs_tle_t) <= ADCS_UTC_RECORD_OFFSET, "the TLE record runs into the UTC record");
_Static_assert(ADCS_UTC_RECORD_OFFSET + MRAM_RECORD_OVERHEAD + sizeof(uint32_t) <= MRAM_ADCS_SIZE,
               "ADCS records must fit their MRAM region");

static bool mram_usable = false;

/* ---------- PUBLIC FUNCTIONS ---------- */

bool adcs_persist_init(void) {
    const mram_status_t status = mram_map_init();

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
    if (mram_usable) {
        (void)mram_record_save(MRAM_REGION_ADCS, ADCS_TLE_RECORD_OFFSET, tle, sizeof(*tle));
    }
}

bool adcs_persist_load_tle(adcs_tle_t *const tle) {
    return mram_usable && mram_record_load(MRAM_REGION_ADCS, ADCS_TLE_RECORD_OFFSET, tle, sizeof(*tle));
}

void adcs_persist_save_utc(const uint32_t unix_time) {
    if (mram_usable) {
        (void)mram_record_save(MRAM_REGION_ADCS, ADCS_UTC_RECORD_OFFSET, &unix_time, sizeof(unix_time));
    }
}

bool adcs_persist_load_utc(uint32_t *const unix_time) {
    return mram_usable && mram_record_load(MRAM_REGION_ADCS, ADCS_UTC_RECORD_OFFSET, unix_time, sizeof(*unix_time));
}
