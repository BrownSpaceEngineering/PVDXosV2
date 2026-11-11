#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "boot_log.h"
#include "boot_record.h"
#include "flash_driver.h"
#include "mram_driver.h"
#include "watchdog_driver.h"

#define SCB_VTOR (*(volatile uint32_t *)0xE000ED08u)

static uint8_t block_buffer[FLASH_REPAIR_BLOCK_SIZE] __attribute__((aligned(4)));
static volatile boot_record_t *const boot_record = (volatile boot_record_t *)BOOT_RECORD_ADDR;

static bool mram_is_available(mram_status_t status) {
    for (uint8_t module = 1; module <= 3; module++) {
        uint8_t module_state = get_mram_state(status, module);
        if (module_state == MRAM_STATE_OK || module_state == MRAM_STATE_DEGRADED) {
            return true;
        }
    }
    return false;
}

static bool header_is_valid(const image_header_t *header) {
    return header->magic == IMAGE_MAGIC && header->app_block_count != 0u && header->app_block_count <= FLASH_OS_APP_BLOCK_COUNT &&
        header->app_size == header->app_block_count * IMAGE_BLOCK_SIZE &&
        crc32((const uint8_t *)header, offsetof(image_header_t, header_crc)) == header->header_crc;
}

// Stage-2 override of the driver's weak hook: one line per MRAM page so a long refresh is visibly progressing.
void mram_write_page_hook(uint32_t address, uint32_t size, uint8_t written, uint8_t failed) {
    boot_log("MRAM wr %06x+%u ok%x fail%x\n", (unsigned int)address, (unsigned int)size, written, failed);
}

static bool copy_flash_to_mram(uint32_t flash_address, uint32_t mram_address, uint32_t size) {
    for (uint32_t offset = 0; offset < size; offset += IMAGE_BLOCK_SIZE) {
        uint32_t chunk_size = size - offset;
        if (chunk_size > IMAGE_BLOCK_SIZE) {
            chunk_size = IMAGE_BLOCK_SIZE;
        }
        memcpy(block_buffer, (const void *)(flash_address + offset), chunk_size);
        uint32_t expected_crc = crc32(block_buffer, chunk_size);
        bool copied = !mram_write_bytes(mram_address + offset, block_buffer, chunk_size) &&
            !mram_read_bytes(mram_address + offset, block_buffer, chunk_size) && crc32(block_buffer, chunk_size) == expected_crc;
        boot_log("Refresh %06x->%06x %s\n", (unsigned int)(flash_address + offset), (unsigned int)(mram_address + offset),
                 copied ? "verified" : "FAILED");
        if (!copied) {
            boot_record->refresh_fail_addr = mram_address + offset;
            return false;
        }
        boot_record->refresh_blocks++;
        watchdog_pet();
    }
    return true;
}

/*
 * Ground-only: make the flash image the MRAM golden copy. The golden header is invalidated first and written last,
 * so an interrupted refresh leaves MRAM without a golden image (repair disabled) instead of a mixed one.
 */
static boot_refresh_t refresh_mram_from_flash(void) {
    image_header_t header;
    memcpy(&header, (const void *)FLASH_OS_ADDR, sizeof(header));
    if (!header_is_valid(&header)) {
        boot_log("Refresh: bad flash header\n");
        return BOOT_REFRESH_BAD_FLASH;
    }
    for (uint32_t block = 0; block < header.app_block_count; block++) {
        watchdog_pet();
        if (crc32((const uint8_t *)(FLASH_OS_APP_ADDR + block * IMAGE_BLOCK_SIZE), IMAGE_BLOCK_SIZE) != header.app_block_crc[block]) {
            boot_log("Refresh: bad flash block %u\n", (unsigned int)block);
            return BOOT_REFRESH_BAD_FLASH;
        }
    }
    boot_log("Refresh: start, %u app blocks\n", (unsigned int)header.app_block_count);

    static const uint8_t invalid_magic[sizeof(header.magic)] = {0};
    if (mram_write_bytes(MRAM_OS_ADDR + offsetof(image_header_t, magic), invalid_magic, sizeof(invalid_magic))) {
        boot_record->refresh_fail_addr = MRAM_OS_ADDR;
        return BOOT_REFRESH_WRITE_FAILED;
    }
    if (!copy_flash_to_mram(FLASH_OS_APP_ADDR, MRAM_OS_ADDR + FLASH_OS_HEADER_SIZE, header.app_size) ||
        !copy_flash_to_mram(FLASH_OS_ADDR, MRAM_OS_ADDR, FLASH_OS_HEADER_SIZE) ||
        !copy_flash_to_mram(FLASH_STAGE1_ADDR, MRAM_BOOTLOADER_ADDR, MRAM_BOOTLOADER_SIZE)) {
        return BOOT_REFRESH_WRITE_FAILED;
    }
    return BOOT_REFRESH_OK;
}

static boot_repair_t repair_os_from_mram(void) {
    if (mram_read_bytes(MRAM_OS_ADDR, block_buffer, IMAGE_HEADER_READ_SIZE)) {
        return BOOT_REPAIR_HEADER_READ_FAILED;
    }

    image_header_t reference_header;
    memcpy(&reference_header, block_buffer, sizeof(reference_header));
    if (!header_is_valid(&reference_header)) {
        return BOOT_REPAIR_NO_IMAGE;
    }

    // MRAM reads are slow (every replica is read for voting), so only fetch what flash gets wrong.
    // The header row holds the header in its first IMAGE_HEADER_READ_SIZE bytes; the rest is padding.
    if (memcmp((const void *)FLASH_OS_ADDR, block_buffer, IMAGE_HEADER_READ_SIZE) != 0) {
        if (mram_read_bytes(MRAM_OS_ADDR, block_buffer, IMAGE_BLOCK_SIZE)) {
            return BOOT_REPAIR_HEADER_READ_FAILED;
        }
        bool programmed = flash_program_block(FLASH_OS_ADDR, block_buffer);
        boot_record->header_rewritten = 1u;
        boot_log("Repair: header row %s\n", programmed ? "fixed" : "FAILED");
    }

    for (uint32_t block = 0; block < reference_header.app_block_count; block++) {
        uint32_t flash_address = FLASH_OS_APP_ADDR + block * IMAGE_BLOCK_SIZE;
        uint32_t mram_address = MRAM_OS_ADDR + FLASH_OS_HEADER_SIZE + block * IMAGE_BLOCK_SIZE;
        watchdog_pet();
        boot_record->blocks_checked++;

        if (crc32((const uint8_t *)flash_address, IMAGE_BLOCK_SIZE) == reference_header.app_block_crc[block]) {
            continue; // Flash already matches the golden copy.
        }
        if (mram_read_bytes(mram_address, block_buffer, IMAGE_BLOCK_SIZE) ||
            crc32(block_buffer, IMAGE_BLOCK_SIZE) != reference_header.app_block_crc[block]) {
            boot_record->blocks_bad_mram++;
            boot_log("Repair: block %u MRAM bad\n", (unsigned int)block);
            continue;
        }
        bool programmed = flash_program_block(flash_address, block_buffer);
        if (programmed) {
            boot_record->blocks_repaired++;
        } else {
            boot_record->blocks_program_failed++;
        }
        boot_log("Repair: block %u %s\n", (unsigned int)block, programmed ? "fixed" : "FAILED");
    }
    boot_log("Repair: %u checked, %u fixed\n", (unsigned int)boot_record->blocks_checked, (unsigned int)boot_record->blocks_repaired);
    return BOOT_REPAIR_DONE;
}

static void go_to_app(void) {
    watchdog_pet();
    boot_log("Stage 2: start OS\n");
    boot_log_shutdown();
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
    if (boot_record->magic != BOOT_RECORD_MAGIC) {
        // Entered without stage 1 (e.g. a debugger): start a record so the OS still sees what stage 2 did.
        memset((void *)boot_record, 0, sizeof(*boot_record));
        boot_record->magic = BOOT_RECORD_MAGIC;
    }
    mram_status_t mram_status = mram_init(); // Also runs atmel_start_init(), which delay_ms() needs.
    boot_log_init();
    boot_log("Stage 2 slot %u, MRAM %02x\n", (unsigned int)boot_record->stage2_slot, mram_status);
    boot_record->mram_status_init = mram_status;
    bool mram_available = mram_is_available(mram_status);

    flash_disable_caches();
    volatile uint32_t *request = (volatile uint32_t *)MRAM_REFRESH_REQUEST_ADDR;
    if (*request == MRAM_REFRESH_REQUEST_MAGIC) {
        boot_refresh_t refresh = mram_available ? refresh_mram_from_flash() : BOOT_REFRESH_NO_MRAM;
        boot_record->refresh = refresh;
        if (refresh == BOOT_REFRESH_OK) {
            *request = 0u; // Only a completed refresh retires the request; anything else is retried next boot.
            boot_log("Refresh: done, request cleared\n");
        } else {
            boot_log("Refresh: incomplete (%u), retry next boot\n", (unsigned int)refresh);
        }
    }

    boot_record->repair = mram_available ? repair_os_from_mram() : BOOT_REPAIR_NO_MRAM;
    boot_record->mram_status_final = mram_get_status();
    go_to_app();
}
