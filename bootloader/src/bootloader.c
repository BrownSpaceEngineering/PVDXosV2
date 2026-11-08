#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "flash_driver.h"
#include "mram_driver.h"
#include "watchdog_driver.h"

#define WRITE_TO_MRAM_REQUEST_ADDRESS (0x47001FFCu)
#define WRITE_TO_MRAM_REQUEST_MAGIC (0x4D524D57u)
#define SCB_VTOR (*(volatile uint32_t *)0xE000ED08u)

static uint8_t block_buffer[FLASH_REPAIR_BLOCK_SIZE] __attribute__((aligned(4)));

static bool mram_is_available(mram_status_t status) {
    for (uint8_t module = 1; module <= 3; module++) {
        uint8_t module_state = get_mram_state(status, module);
        if (module_state == MRAM_STATE_OK || module_state == MRAM_STATE_DEGRADED) {
            return true;
        }
    }
    return false;
}

static bool copy_flash_to_mram(uint32_t flash_address, uint32_t mram_address, uint32_t size) {
    for (uint32_t offset = 0; offset < size; offset += IMAGE_BLOCK_SIZE) {
        uint32_t chunk_size = size - offset;
        if (chunk_size > IMAGE_BLOCK_SIZE) {
            chunk_size = IMAGE_BLOCK_SIZE;
        }
        memcpy(block_buffer, (const void *)(flash_address + offset), chunk_size);
        uint32_t expected_crc = crc32(block_buffer, chunk_size);
        if (mram_write_bytes(mram_address + offset, block_buffer, chunk_size) ||
            mram_read_bytes(mram_address + offset, block_buffer, chunk_size) || crc32(block_buffer, chunk_size) != expected_crc) {
            return false;
        }
        watchdog_pet();
    }
    return true;
}

static void refresh_mram_from_flash(void) {
    copy_flash_to_mram(FLASH_OS_ADDR, MRAM_OS_ADDR, MRAM_OS_SIZE);
    copy_flash_to_mram(FLASH_STAGE1_ADDR, MRAM_BOOTLOADER_ADDR, MRAM_BOOTLOADER_SIZE);
}

static void repair_os_from_mram(void) {
    if (mram_read_bytes(MRAM_OS_ADDR, block_buffer, IMAGE_HEADER_READ_SIZE)) {
        return;
    }

    image_header_t reference_header;
    memcpy(&reference_header, block_buffer, sizeof(reference_header));
    if (reference_header.magic != IMAGE_MAGIC || reference_header.app_block_count == 0u ||
        reference_header.app_block_count > FLASH_OS_APP_BLOCK_COUNT ||
        reference_header.app_size != reference_header.app_block_count * IMAGE_BLOCK_SIZE ||
        crc32((const uint8_t *)&reference_header, offsetof(image_header_t, header_crc)) != reference_header.header_crc) {
        return;
    }

    if (mram_read_bytes(MRAM_OS_ADDR, block_buffer, IMAGE_BLOCK_SIZE)) {
        return;
    }
    if (memcmp((const void *)FLASH_OS_ADDR, block_buffer, IMAGE_BLOCK_SIZE) != 0) {
        flash_program_block(FLASH_OS_ADDR, block_buffer);
    }

    for (uint32_t block = 0; block < reference_header.app_block_count; block++) {
        uint32_t flash_address = FLASH_OS_APP_ADDR + block * IMAGE_BLOCK_SIZE;
        uint32_t mram_address = MRAM_OS_ADDR + FLASH_OS_HEADER_SIZE + block * IMAGE_BLOCK_SIZE;
        watchdog_pet();

        if (mram_read_bytes(mram_address, block_buffer, IMAGE_BLOCK_SIZE) ||
            crc32(block_buffer, IMAGE_BLOCK_SIZE) != reference_header.app_block_crc[block]) {
            continue;
        }
        if (crc32((const uint8_t *)flash_address, IMAGE_BLOCK_SIZE) != reference_header.app_block_crc[block]) {
            flash_program_block(flash_address, block_buffer);
        }
    }
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
    mram_status_t mram_status = mram_init();
    bool mram_available = mram_is_available(mram_status);

    flash_disable_caches();
    volatile uint32_t *request = (volatile uint32_t *)WRITE_TO_MRAM_REQUEST_ADDRESS;
    if (*request == WRITE_TO_MRAM_REQUEST_MAGIC) {
        *request = 0u;
        if (mram_available) {
            refresh_mram_from_flash();
        }
    }

    if (mram_available) {
        repair_os_from_mram();
    }

    go_to_app();
}