#ifndef BOOT_RECORD_H
#define BOOT_RECORD_H

#include <stdint.h>

/*
 * The last 64 bytes of backup RAM are shared by stage 1, stage 2, the OS and the ground tooling. Both linker
 * scripts end their bkupram region at BOOT_SHARED_BKUPRAM_ADDR so nothing else is placed here. Backup RAM
 * survives system/watchdog/external resets but not a power cycle.
 */
#define BOOT_SHARED_BKUPRAM_ADDR (0x47001FC0u)
#define BOOT_SHARED_BKUPRAM_SIZE (0x00000040u)

// Ground-only request (set by tools/mram_refresh.sh): stage 2 copies the flash OS image to MRAM.
// Stage 2 clears it only after a successful refresh, so an interrupted or failed refresh is retried next boot.
#define MRAM_REFRESH_REQUEST_ADDR (0x47001FFCu)
#define MRAM_REFRESH_REQUEST_MAGIC (0x4D524D57u)

// What happened during this boot. Stage 1 starts it, stage 2 fills it in, the OS logs it and invalidates it.
#define BOOT_RECORD_ADDR (0x47001FC0u)
#define BOOT_RECORD_MAGIC (0x42524543u)

#define BOOT_STAGE2_SLOT_UNKNOWN (0x00u)  // Stage 2 was entered without stage 1 (e.g. a debugger).
#define BOOT_STAGE2_SLOT_FALLBACK (0xFFu) // Every stage-2 copy failed its CRC; stage 1 jumped straight to the OS.

typedef enum {
    BOOT_REFRESH_NOT_REQUESTED = 0,
    BOOT_REFRESH_OK = 1,
    BOOT_REFRESH_NO_MRAM = 2,      // Request kept: no healthy MRAM module.
    BOOT_REFRESH_BAD_FLASH = 3,    // Request kept: flash image fails its own header/CRCs, MRAM untouched.
    BOOT_REFRESH_WRITE_FAILED = 4, // Request kept: MRAM golden header left invalid until a refresh completes.
} boot_refresh_t;

typedef enum {
    BOOT_REPAIR_NOT_RUN = 0, // Stage 2 did not run (or stopped before repair).
    BOOT_REPAIR_NO_MRAM = 1,
    BOOT_REPAIR_HEADER_READ_FAILED = 2,
    BOOT_REPAIR_NO_IMAGE = 3, // MRAM holds no valid golden header.
    BOOT_REPAIR_DONE = 4,
} boot_repair_t;

typedef struct {
    uint32_t magic;
    uint8_t stage2_slot;       // 1-8, or BOOT_STAGE2_SLOT_*.
    uint8_t mram_status_init;  // mram_status_t after mram_init().
    uint8_t mram_status_final; // mram_status_t when stage 2 handed over (modules may fail during the boot).
    uint8_t refresh;           // boot_refresh_t
    uint8_t repair;            // boot_repair_t
    uint8_t header_rewritten;  // Flash header row differed from MRAM and was reprogrammed.
    uint16_t blocks_checked;   // App blocks compared against the golden CRCs.
    uint16_t blocks_repaired;  // Flash blocks reprogrammed from MRAM.
    uint16_t blocks_bad_mram;  // Flash block wrong, but MRAM copy unreadable or failed its CRC.
    uint16_t blocks_program_failed;
    uint16_t refresh_blocks;    // 8 KiB blocks copied to MRAM by the refresh.
    uint32_t refresh_fail_addr; // MRAM address of the block that failed to copy.
} boot_record_t;

_Static_assert(BOOT_RECORD_ADDR + sizeof(boot_record_t) <= MRAM_REFRESH_REQUEST_ADDR, "boot record overlaps request");
_Static_assert(MRAM_REFRESH_REQUEST_ADDR + 4u <= BOOT_SHARED_BKUPRAM_ADDR + BOOT_SHARED_BKUPRAM_SIZE, "request outside shared backup RAM");

#endif
