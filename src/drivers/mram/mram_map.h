#ifndef MRAM_MAP_H
#define MRAM_MAP_H

/*
 * Where PVDXos keeps data in MRAM, and the only way PVDXos should reach it. Each user gets a fixed region, and
 * offsets are relative to that region, so a bug in one user can't overwrite another's data or the bootloader's golden
 * copy. Calls from different tasks are serialized by a mutex.
 *
 * Addresses are fixed rather than packed, so growing one region never moves the others: data saved by one build is
 * still found by the next. To add a region, take free space after the last one, add it to mram_region_t and to the
 * table and checks in mram_map.c.
 *
 *   0x000000  bootloader golden copy of flash (bootloader/src/image_format.h)    508 KB
 *   0x07F000  unused guard                                                         4 KB
 *   0x080000  MRAM_REGION_BURN_RECORD                                              4 KB
 *   0x081000  MRAM_REGION_ADCS                                                     4 KB
 *   0x082000  MRAM_REGION_PEROVSKITE                                              32 KB
 *   0x08A000  free                                                                24 KB
 *   0x090000  MRAM_REGION_CAMERA                                                 388 KB
 *   0x0F1000  free, to the end of the chip at 0x100000                            60 KB
 */

#include <stdbool.h>
#include <stdint.h>

#include "mram_driver.h"

#define MRAM_CHIP_SIZE (0x100000u) // EM008LX: 8 Mb

#define MRAM_OS_START (0x080000u) // First address PVDXos may use

#define MRAM_BURN_RECORD_ADDRESS (0x080000u)
#define MRAM_BURN_RECORD_SIZE (0x1000u)
#define MRAM_ADCS_ADDRESS (0x081000u)
#define MRAM_ADCS_SIZE (0x1000u)
#define MRAM_PEROVSKITE_ADDRESS (0x082000u)
#define MRAM_PEROVSKITE_SIZE (0x8000u)
#define MRAM_CAMERA_ADDRESS (0x090000u)
#define MRAM_CAMERA_SIZE (0x61000u)

typedef enum {
    MRAM_REGION_BURN_RECORD, // main.c's burn wait progress
    MRAM_REGION_ADCS,        // TLE and UTC, see adcs_persist.c
    MRAM_REGION_PEROVSKITE,  // Sweep ring buffer, see perovskite_task.h
    MRAM_REGION_CAMERA,      // Last JPEG from capture_mram(), see arducam_driver.h
    MRAM_REGION_COUNT,
} mram_region_t;

// Bytes of MRAM a record takes beyond its payload
#define MRAM_RECORD_OVERHEAD (8u)

// mram_init(). Called once, by main.c before the scheduler starts; nothing else may call it or mram_init().
mram_status_t mram_map_init(void);
// What mram_map_init() found, for anything that needs to know which chips work
mram_status_t mram_map_status(void);

// Read or write `size` bytes at `offset` within `region`. Return false if the range doesn't fit in the region or the
// transfer fails. Reads come from the first chip mram_init() found working; writes go to all three, and return false
// if any chip's write fails.
bool mram_region_read(mram_region_t region, uint32_t offset, void *data, uint32_t size);
bool mram_region_write(mram_region_t region, uint32_t offset, const void *data, uint32_t size);

/*
 * Records: a payload of `size` bytes saved at `offset` within `region` with its size and a CRC, taking up
 * MRAM_RECORD_OVERHEAD + size bytes. A record saved with a different size is rejected, but one with the same size and
 * a different layout is not, so a payload whose layout changes should also change size or move.
 *
 * mram_record_save() writes the chips one after another, so a reset part way through leaves at most one chip torn.
 * It returns false if any chip's write fails.
 *
 * mram_record_load() uses the first chip whose copy has the right size and CRC, then writes that copy back to
 * the other two, so one bad chip never leaves the record unprotected. It returns false, with `payload` overwritten,
 * if no chip has a valid copy.
 */
bool mram_record_save(mram_region_t region, uint32_t offset, const void *payload, uint32_t size);
bool mram_record_load(mram_region_t region, uint32_t offset, void *payload, uint32_t size);

#endif // MRAM_MAP_H
