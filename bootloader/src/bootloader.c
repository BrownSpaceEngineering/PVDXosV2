#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "boot_record.h"
#include "flash_driver.h"
#include "mram_driver.h"
#include "watchdog_driver.h"

#define SCB_VTOR (*(volatile uint32_t *)0xE000ED08u)
// The OS's SEGGER RTT control block (rtt region in src/src_flash.ld); its first 16 bytes are the "SEGGER RTT" ID.
#define RTT_CONTROL_BLOCK_ADDR (0x20000000u)
#define RTT_CONTROL_BLOCK_ID_SIZE (16u)

static uint8_t block_buffer[FLASH_REPAIR_BLOCK_SIZE] __attribute__((aligned(4)));
static volatile boot_record_t *const boot_record = (volatile boot_record_t *)BOOT_RECORD_ADDR;
_Static_assert(FLASH_OS_APP_BLOCK_COUNT <= 64u, "boot record block masks hold 64 app blocks");

static bool header_is_valid(const image_header_t *header) {
    return header->magic == IMAGE_MAGIC && header->app_block_count != 0u && header->app_block_count <= FLASH_OS_APP_BLOCK_COUNT &&
        header->app_size == header->app_block_count * IMAGE_BLOCK_SIZE &&
        crc32((const uint8_t *)header, offsetof(image_header_t, header_crc)) == header->header_crc;
}

// Checks for data read from MRAM into block_buffer.
typedef bool (*mram_check_t)(uint32_t size, uint32_t arg);

static bool block_buffer_crc_matches(uint32_t size, uint32_t expected_crc) {
    return crc32(block_buffer, size) == expected_crc;
}

static bool block_buffer_header_valid(uint32_t size, uint32_t unused) {
    (void)size;
    (void)unused;
    image_header_t header;
    memcpy(&header, block_buffer, sizeof(header));
    return header_is_valid(&header);
}

/*
 * Reads MRAM into block_buffer and accepts it only once `check` passes. The voted read comes first; if it fails or
 * does not pass, each module is tried on its own, so the check picks the good copy where a vote cannot (two modules
 * that disagree, or the same bit flipped in two replicas). Accepted data that some replica disagreed with is written
 * back to every module: the driver never writes back on its own, because an unchecked vote can be wrong.
 */
static bool mram_read_verified(uint32_t mram_address, uint32_t size, mram_check_t check, uint32_t arg) {
    bool replicas_differ = false;
    bool verified = !mram_read_bytes(mram_address, block_buffer, size, &replicas_differ) && check(size, arg);
    for (uint8_t mram = 1; !verified && mram <= 3; mram++) {
        replicas_differ = true; // The vote failed, so at least one replica is wrong (or unreadable).
        if (mram_read_module(mram, mram_address, block_buffer, size)) {
            continue; // Unreadable: the driver marked it failed.
        }
        verified = check(size, arg);
        if (!verified) {
            mram_mark_degraded(mram); // Readable, but its own copy is wrong.
        }
    }
    if (verified && replicas_differ) {
        boot_record->mram_scrubs++;
        (void)mram_write_bytes(mram_address, block_buffer, size); // A failing module is marked failed by the driver.
    }
    return verified;
}

typedef enum {
    FLASH_ECC_NONE = 0,
    FLASH_ECC_SINGLE = 1, // Corrected on read: the data is right, but a bit in flash is flipped.
    FLASH_ECC_DUAL = 2,   // Uncorrectable: the data read back is wrong.
} flash_ecc_t;

/*
 * Reads (and so clears) NVMCTRL ECCERR and classifies any error inside [address, address + size). ECCERR keeps only
 * the first error since it was last read, and stage 2's own code fetches go through ECC too, so callers clear it with
 * get_eccerr_status() just before reading the range. Every error is kept in boot_record->ecc_last_err (raw register:
 * ADDR in bits 23:0, TYPEL 29:28, TYPEH 31:30), but only one inside the range is reported to the caller.
 */
static flash_ecc_t flash_ecc_check(uint32_t address, uint32_t size) {
    uint32_t eccerr = get_eccerr_status();
    if ((eccerr & (NVMCTRL_ECCERR_TYPEL_Msk | NVMCTRL_ECCERR_TYPEH_Msk)) == 0u) {
        return FLASH_ECC_NONE;
    }

    boot_record->ecc_last_err = eccerr;
    uint32_t error_address = (eccerr & NVMCTRL_ECCERR_ADDR_Msk) >> NVMCTRL_ECCERR_ADDR_Pos;
    if (error_address - address >= size) { // Unsigned: also catches addresses below the range.
        return FLASH_ECC_NONE;
    }
    return (eccerr & (NVMCTRL_ECCERR_TYPEL_DUAL | NVMCTRL_ECCERR_TYPEH_DUAL)) != 0u ? FLASH_ECC_DUAL : FLASH_ECC_SINGLE;
}

// CRC of a flash range, reporting any ECC error the read hit inside it.
static uint32_t crc32_flash_ecc(uint32_t address, uint32_t size, flash_ecc_t *ecc) {
    (void)get_eccerr_status();
    uint32_t crc = crc32((const uint8_t *)address, size);
    *ecc = flash_ecc_check(address, size);
    return crc;
}

static bool copy_flash_to_mram(uint32_t flash_address, uint32_t mram_address, uint32_t size) {
    for (uint32_t offset = 0; offset < size; offset += IMAGE_BLOCK_SIZE) {
        uint32_t chunk_size = size - offset;
        if (chunk_size > IMAGE_BLOCK_SIZE) {
            chunk_size = IMAGE_BLOCK_SIZE;
        }
        memcpy(block_buffer, (const void *)(flash_address + offset), chunk_size);
        uint32_t expected_crc = crc32(block_buffer, chunk_size);
        // Every healthy replica must read back identical: a module that silently missed the write would differ.
        bool replicas_differ = true;
        bool copied = !mram_write_bytes(mram_address + offset, block_buffer, chunk_size) &&
            !mram_read_bytes(mram_address + offset, block_buffer, chunk_size, &replicas_differ) && !replicas_differ &&
            crc32(block_buffer, chunk_size) == expected_crc;
        if (!copied) {
            boot_record->refresh_fail_addr = mram_address + offset;
            return false;
        }
        boot_record->refresh_blocks++; // Counts up live: tools/mram_refresh.sh status shows the progress.
        watchdog_pet();
    }
    return true;
}

/*
 * Ground-only: make the flash image the MRAM golden copy. The golden header is invalidated first and written last,
 * so an interrupted refresh leaves MRAM without a golden image (repair disabled) instead of a mixed one.
 * A module that fails mid-refresh is only failed in RAM: next boot it is healthy again with stale data and votes.
 * So the header is written only while at least 2 fresh copies remain (one fresh copy would be outvoted), and a
 * refresh that lost any module keeps the request, so it reruns with every module.
 */
static boot_refresh_t refresh_mram_from_flash(void) {
    mram_status_t start_status = mram_get_status();
    if (mram_healthy_count(start_status) < 2u) {
        return BOOT_REFRESH_MODULE_LOST;
    }
    image_header_t header;
    memcpy(&header, (const void *)FLASH_OS_ADDR, sizeof(header));
    if (!header_is_valid(&header)) {
        boot_record->refresh_fail_addr = FLASH_OS_ADDR;
        return BOOT_REFRESH_BAD_FLASH;
    }
    for (uint32_t block = 0; block < header.app_block_count; block++) {
        watchdog_pet();
        uint32_t flash_address = FLASH_OS_APP_ADDR + block * IMAGE_BLOCK_SIZE;
        flash_ecc_t ecc;
        uint32_t crc = crc32_flash_ecc(flash_address, IMAGE_BLOCK_SIZE, &ecc);
        // A dual ECC error means flash returned uncorrected data; never promote it to the golden copy.
        if (crc != header.app_block_crc[block] || ecc == FLASH_ECC_DUAL) {
            boot_record->refresh_fail_addr = flash_address;
            return BOOT_REFRESH_BAD_FLASH;
        }
    }

    static const uint8_t invalid_magic[sizeof(header.magic)] = {0};
    if (mram_write_bytes(MRAM_OS_ADDR + offsetof(image_header_t, magic), invalid_magic, sizeof(invalid_magic))) {
        boot_record->refresh_fail_addr = MRAM_OS_ADDR;
        return BOOT_REFRESH_WRITE_FAILED;
    }
    if (!copy_flash_to_mram(FLASH_OS_APP_ADDR, MRAM_OS_ADDR + FLASH_OS_HEADER_SIZE, header.app_size)) {
        return BOOT_REFRESH_WRITE_FAILED;
    }
    if (mram_healthy_count(mram_get_status()) < 2u) {
        return BOOT_REFRESH_MODULE_LOST; // Golden header stays invalid.
    }
    if (!copy_flash_to_mram(FLASH_OS_ADDR, MRAM_OS_ADDR, FLASH_OS_HEADER_SIZE) ||
        !copy_flash_to_mram(FLASH_STAGE1_ADDR, MRAM_BOOTLOADER_ADDR, MRAM_BOOTLOADER_SIZE)) {
        return BOOT_REFRESH_WRITE_FAILED;
    }
    return mram_get_status() == start_status ? BOOT_REFRESH_OK : BOOT_REFRESH_MODULE_LOST;
}

static boot_repair_t repair_os_from_mram(void) {
    if (!mram_read_verified(MRAM_OS_ADDR, IMAGE_HEADER_READ_SIZE, block_buffer_header_valid, 0u)) {
        return mram_healthy_count(mram_get_status()) == 0u ? BOOT_REPAIR_HEADER_READ_FAILED : BOOT_REPAIR_NO_IMAGE;
    }
    image_header_t reference_header;
    memcpy(&reference_header, block_buffer, sizeof(reference_header));

    // MRAM reads are slow (every replica is read for voting), so only fetch what flash gets wrong.
    // The header row holds the header in its first IMAGE_HEADER_READ_SIZE bytes; the rest is padding.
    // Any ECC error in the header also forces a rewrite: a single error is a flipped bit that will not heal itself.
    (void)get_eccerr_status();
    bool header_differs = memcmp((const void *)FLASH_OS_ADDR, block_buffer, IMAGE_HEADER_READ_SIZE) != 0;
    if (flash_ecc_check(FLASH_OS_ADDR, IMAGE_HEADER_READ_SIZE) != FLASH_ECC_NONE || header_differs) {
        if (!mram_read_verified(MRAM_OS_ADDR, IMAGE_BLOCK_SIZE, block_buffer_header_valid, 0u)) {
            return BOOT_REPAIR_HEADER_READ_FAILED;
        }
        boot_record->header_row = flash_program_block(FLASH_OS_ADDR, block_buffer) ? BOOT_HEADER_ROW_REWRITTEN : BOOT_HEADER_ROW_FAILED;
    }

    for (uint32_t block = 0; block < reference_header.app_block_count; block++) {
        uint32_t flash_address = FLASH_OS_APP_ADDR + block * IMAGE_BLOCK_SIZE;
        uint32_t mram_address = MRAM_OS_ADDR + FLASH_OS_HEADER_SIZE + block * IMAGE_BLOCK_SIZE;
        watchdog_pet();
        boot_record->blocks_checked++;

        flash_ecc_t ecc;
        bool crc_ok = crc32_flash_ecc(flash_address, IMAGE_BLOCK_SIZE, &ecc) == reference_header.app_block_crc[block];
        if (ecc == FLASH_ECC_DUAL) {
            boot_record->blocks_ecc_dual++;
        }
        if (crc_ok && ecc == FLASH_ECC_NONE) {
            continue; // Flash already matches the golden copy.
        }
        uint64_t block_bit = (uint64_t)1u << block;
        boot_record->blocks_flagged |= block_bit;
        if (crc_ok && ecc == FLASH_ECC_SINGLE) {
            // ECC corrected the read, so flash still yields the golden data: scrub the flipped bit from that data
            // (re-checked against the golden CRC) instead of the slow MRAM read.
            memcpy(block_buffer, (const void *)flash_address, IMAGE_BLOCK_SIZE);
            bool scrubbed = crc32(block_buffer, IMAGE_BLOCK_SIZE) == reference_header.app_block_crc[block] &&
                flash_program_block(flash_address, block_buffer);
            if (scrubbed) {
                boot_record->blocks_ecc_scrubbed++;
                continue;
            }
            // Fall through to the MRAM copy (flash_program_block may have left the block erased).
        }
        if (!mram_read_verified(mram_address, IMAGE_BLOCK_SIZE, block_buffer_crc_matches, reference_header.app_block_crc[block])) {
            boot_record->blocks_bad_mram++;
            boot_record->blocks_unfixed |= block_bit;
            continue;
        }
        bool programmed = flash_program_block(flash_address, block_buffer);
        if (programmed) {
            boot_record->blocks_repaired++;
        } else {
            boot_record->blocks_program_failed++;
            boot_record->blocks_unfixed |= block_bit;
        }
    }
    return BOOT_REPAIR_DONE;
}

static void go_to_app(void) {
    watchdog_pet();
    spi_m_sync_disable(&SPI_MRAM);
    NVIC_DisableIRQ(NVMCTRL_0_IRQn);
    NVIC_DisableIRQ(NVMCTRL_1_IRQn);
    flash_enable_caches();

    const uint32_t *vector_table = (const uint32_t *)FLASH_OS_APP_ADDR;
    uint32_t stack_pointer = vector_table[0];
    uint32_t reset_handler = vector_table[1] | 1u;
    SCB_VTOR = FLASH_OS_APP_ADDR;
    __asm__ volatile("mov sp, %0\n bx %1" : : "r"(stack_pointer), "r"(reset_handler) : "memory");
    __builtin_unreachable();
}

int main(void) {
    watchdog_setup();
    // RAM survives a reset, so the last OS's RTT control block is still there. Hide it until the OS rebuilds it, so an
    // attached J-Link does not follow its buffer pointers into RAM that stage 2 is about to reuse.
    memset((void *)RTT_CONTROL_BLOCK_ADDR, 0, RTT_CONTROL_BLOCK_ID_SIZE);
    if (boot_record->magic != BOOT_RECORD_MAGIC) {
        // Entered without stage 1 (e.g. a debugger): start a record so the OS still sees what stage 2 did.
        memset((void *)boot_record, 0, sizeof(*boot_record));
        boot_record->magic = BOOT_RECORD_MAGIC;
    }
    atmel_start_init();
    mram_status_t mram_status = mram_init();
    boot_record->mram_status_init = mram_status;
    bool mram_available = mram_healthy_count(mram_status) != 0u;

    flash_disable_caches();
    volatile uint32_t *request = (volatile uint32_t *)MRAM_REFRESH_REQUEST_ADDR;
    if (*request == MRAM_REFRESH_REQUEST_MAGIC) {
        boot_refresh_t refresh = mram_available ? refresh_mram_from_flash() : BOOT_REFRESH_NO_MRAM;
        boot_record->refresh = refresh;
        if (refresh == BOOT_REFRESH_OK) {
            *request = 0u; // Only a completed refresh retires the request; anything else is retried next boot.
        }
    }

    boot_record->repair = mram_available ? repair_os_from_mram() : BOOT_REPAIR_NO_MRAM;
    boot_record->mram_status_final = mram_get_status();
    go_to_app();
}
