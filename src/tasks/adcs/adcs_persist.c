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
#include "logging.h"
#include "tasks/adcs/adcs_task.h"

// Offsets of the records in the ADCS MRAM region
#define ADCS_TLE_RECORD_OFFSET 0x000u
#define ADCS_UTC_RECORD_OFFSET 0x100u

_Static_assert(MRAM_RECORD_OVERHEAD + sizeof(adcs_tle_t) <= ADCS_UTC_RECORD_OFFSET, "the TLE record runs into the UTC record");
_Static_assert(ADCS_UTC_RECORD_OFFSET + MRAM_RECORD_OVERHEAD + sizeof(uint32_t) <= MRAM_ADCS_SIZE,
               "ADCS records must fit their MRAM region");

/* ---------- PUBLIC FUNCTIONS ---------- */

// A mock build (make ADCS_MOCK=1) runs on a simulated orbit and clock, which must not replace the real saved state
#ifdef ADCS_MOCK
    #define ADCS_PERSIST_SKIP_IN_MOCK(ret)                                                                                                 \
        do {                                                                                                                               \
            debug("adcs persist: skipped in mock build\n");                                                                                \
            return ret;                                                                                                                    \
        } while (0)
#else
    #define ADCS_PERSIST_SKIP_IN_MOCK(ret)
#endif

void adcs_persist_save_tle(const adcs_tle_t *const tle) {
    ADCS_PERSIST_SKIP_IN_MOCK();
    (void)mram_record_save(MRAM_REGION_ADCS, ADCS_TLE_RECORD_OFFSET, tle, sizeof(*tle));
}

bool adcs_persist_load_tle(adcs_tle_t *const tle) {
    ADCS_PERSIST_SKIP_IN_MOCK(false);
    return mram_record_load(MRAM_REGION_ADCS, ADCS_TLE_RECORD_OFFSET, tle, sizeof(*tle));
}

void adcs_persist_save_utc(const uint32_t unix_time) {
    ADCS_PERSIST_SKIP_IN_MOCK();
    (void)mram_record_save(MRAM_REGION_ADCS, ADCS_UTC_RECORD_OFFSET, &unix_time, sizeof(unix_time));
}

bool adcs_persist_load_utc(uint32_t *const unix_time) {
    ADCS_PERSIST_SKIP_IN_MOCK(false);
    return mram_record_load(MRAM_REGION_ADCS, ADCS_UTC_RECORD_OFFSET, unix_time, sizeof(*unix_time));
}
