#include "atmel_start.h"
#include "mram.h"

extern struct flash_descriptor FLASH_0;

#define FLASH_BLOCK_SIZE 8192
#define FLASK_BLOCK_COUNT (MRAM_FLASH_SIZE / FLASH_BLOCK_SIZE)

uint8_t stored_checksums[FLASK_BLOCK_COUNT];
bool stored_checksums_initialized = false;

uint32_t crc32_table[256];
bool crc32_table_ready = false;

void crc32_init_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
        crc32_table[i] = crc;
    }
    crc32_table_ready = true;
}

uint32_t crc32_block(const uint8_t *block) {
    if (!crc32_table_ready) crc32_init_table();

    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < FLASH_BLOCK_SIZE; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ block[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

void reflash_bootloaders(void) {
#ifndef MRAM_OS_READ
    return;
#endif

    uint8_t mram_block[FLASH_BLOCK_SIZE];

    for (uint32_t i = 0; i < FLASK_BLOCK_COUNT; i++) {
        uint8_t* flash_block = (uint8_t*)(i * FLASH_BLOCK_SIZE);

        if (stored_checksums_initialized) {
            if (crc32_block(flash_block) == stored_checksums[i]) {
                continue;
            }
        }

        mram_read_bytes(MRAM_FLASH_BASE_ADDRESS + i * FLASH_BLOCK_SIZE, mram_block, FLASH_BLOCK_SIZE);

        stored_checksums[i] = crc32_block(mram_block);

        bool any_error = false;
        for (uint32_t j = 0; j < FLASH_BLOCK_SIZE; j++) {
            if (mram_block[j] != flash_block[j]) {
                any_error = true;
                break;
            }
        }

        if (any_error) {
            flash_erase(&FLASH_0, i * FLASH_BLOCK_SIZE, 1);
            flash_write(&FLASH_0, i * FLASH_BLOCK_SIZE, mram_block, FLASH_BLOCK_SIZE);
        }
    }

    stored_checksums_initialized = true;
}

void main_reflash_task(void *pvParameters) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000 * 60 * 60 * 6));

        vTaskSuspendAll();
        reflash_bootloaders();
        xTaskResumeAll();
    }
}