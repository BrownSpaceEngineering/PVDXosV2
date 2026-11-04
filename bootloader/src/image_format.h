/**
 * image_format.h
 *
 * Flash layout, MRAM golden-copy layout, image header and boot report shared by the bootloader (stage 1 and
 * stage 2), PVDXos (which reads the boot report) and scripts/create_flash_segment.py (which builds the header).
 * Keep scripts/create_flash_segment.py and scripts/gen_bootloaders.py in sync with this file.
 *
 * Created: October 28, 2026
 */

#ifndef IMAGE_FORMAT_H
#define IMAGE_FORMAT_H

#include <stdint.h>

/* ---------- FLASH LAYOUT ---------- */

#define IMAGE_BLOCK_SIZE (0x2000u) // NVM erase block; every region below is a whole number of these

#define FLASH_STAGE1_ADDR (0x00000000u) // Stage 1: picks the first stage-2 copy with a valid CRC32
#define FLASH_STAGE1_SIZE (0x2000u)

#define FLASH_STAGE2_COUNT (3u)
#define FLASH_STAGE2_SIZE (0x4000u) // Each copy ends with the CRC32 of the rest of its slot
#define FLASH_STAGE2_ADDR(index) (0x00002000u + (index) * FLASH_STAGE2_SIZE)

#define FLASH_METADATA_ADDR (0x0000E000u) // One block holding this build's image_header_t
#define FLASH_BOOT_REGION_SIZE (0x00010000u) // Stage 1 + stage-2 copies + metadata

// PVDXos runs from here, above the largest region BOOTPROT can protect (15 blocks, 120 KB), so the bootloader can always
// rewrite it; its size limit is MRAM_GOLDEN_APP_MAX_SIZE
#define FLASH_APP_ADDR (0x0001E000u)
#define BOOTPROT_MAX_SIZE (15u * IMAGE_BLOCK_SIZE)
_Static_assert(FLASH_APP_ADDR >= BOOTPROT_MAX_SIZE, "PVDXos must start above every BOOTPROT region");

/* ---------- MRAM GOLDEN COPY LAYOUT (same on all three chips) ---------- */

#define MRAM_GOLDEN_HEADER_ADDR (0x000000u) // image_header_t (4 KB reserved)
#define MRAM_GOLDEN_HEADER_READ_SIZE (0x200u) // Covers sizeof(image_header_t)
#define MRAM_GOLDEN_BOOT_ADDR (0x001000u) // Copy of the flash boot region
#define MRAM_GOLDEN_APP_ADDR (0x011000u) // Copy of the PVDXos image
#define MRAM_GOLDEN_APP_MAX_SIZE (0x06E000u) // Ends at 0x7F000, clear of the MRAM hardware test region at 0x800F0

/* ---------- IMAGE HEADER ---------- */

#define IMAGE_MAGIC (0x47445650u) // ASCII "PVDG" (little-endian)
#define IMAGE_FORMAT_VERSION (2u)

#define BOOT_REGION_BLOCKS (FLASH_BOOT_REGION_SIZE / IMAGE_BLOCK_SIZE) // 8
#define METADATA_BLOCK_INDEX ((FLASH_METADATA_ADDR - FLASH_STAGE1_ADDR) / IMAGE_BLOCK_SIZE) // 7: holds the header itself
#define APP_MAX_BLOCKS (MRAM_GOLDEN_APP_MAX_SIZE / IMAGE_BLOCK_SIZE) // 55

// Written to the flash metadata block by create_flash_segment.py, and to MRAM when the golden copy is written.
// All fields are little-endian uint32_t, so the Python packer and this struct agree without padding.
typedef struct {
    uint32_t magic;
    uint32_t format_version;
    uint32_t app_size; // Bytes of PVDXos, rounded up to IMAGE_BLOCK_SIZE (padding is 0xFF)
    uint32_t app_block_count;
    uint32_t boot_block_crc[BOOT_REGION_BLOCKS]; // CRC32 of each boot-region block; the metadata entry is unused (0)
    uint32_t app_block_crc[APP_MAX_BLOCKS];      // CRC32 of each PVDXos block; entries past app_block_count are 0
    uint32_t header_crc;                         // CRC32 of every field above
} image_header_t;

/* ---------- WRITE-FLASH-TO-MRAM REQUEST (left in backup RAM by a debugger, see flash_to_mram.jlink) ---------- */

// Backup RAM survives a reset, so a debugger can write this word and then reset the board. Stage 2 clears it and
// copies flash into MRAM on that one boot. Keep the address and value in sync with flash_to_mram.jlink.
#define WRITE_TO_MRAM_REQUEST_ADDR (0x47001FFCu) // Last word of backup RAM
#define WRITE_TO_MRAM_REQUEST_MAGIC (0x4D524D57u) // ASCII "WMRM"

/* ---------- BOOT REPORT (left in backup RAM by stage 2 for PVDXos to log) ---------- */

#define BOOT_REPORT_ADDR (0x47000004u) // Right after the 'PVDX' bootloader magic number at the start of backup RAM
#define BOOT_REPORT_MAGIC (0x52425650u) // ASCII "PVBR"

typedef enum {
    GOLDEN_VALID = 0,    // Flash was checked against the golden copy
    GOLDEN_WRITTEN,      // A debugger requested it: flash was copied into MRAM
    GOLDEN_WRITE_FAILED, // A debugger requested it, but an MRAM write or read-back failed
    GOLDEN_UNREADABLE,   // No MRAM or no valid golden header, so flash was not checked
} golden_state_t;

typedef struct {
    uint32_t magic;
    uint8_t golden_state;     // golden_state_t
    uint8_t mram_status;      // mram_status_t from mram_init()
    uint8_t stage2_index;     // Which stage-2 copy ran (0-2)
    uint16_t blocks_checked;
    uint16_t blocks_repaired;
    uint16_t blocks_failed;   // Blocks that were wrong and could not be repaired (or whose golden data was bad)
} boot_report_t;

#endif // IMAGE_FORMAT_H
