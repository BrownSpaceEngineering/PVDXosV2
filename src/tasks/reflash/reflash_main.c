#include "reflash_task.h"
#include "mram_driver.h"
#include "watchdog_driver.h"

extern struct flash_descriptor FLASH_0;

#define FLASH_BLOCK_SIZE 8192
#define FLASK_BLOCK_COUNT (MRAM_FLASH_SIZE / FLASH_BLOCK_SIZE)

reflash_task_memory_t reflash_mem;

uint8_t stored_checksums[FLASK_BLOCK_COUNT];
bool stored_checksums_initialized = false;

void reflash_bootloaders(void) {
#ifndef MRAM_OS_READ
    return;
#endif

    uint8_t mram_block[FLASH_BLOCK_SIZE];

    for (uint32_t i = 0; i < FLASK_BLOCK_COUNT; i++) {
        uint8_t* flash_block = (uint8_t*)(i * FLASH_BLOCK_SIZE);

        if (stored_checksums_initialized) {
            if (crc32(flash_block, FLASH_BLOCK_SIZE) == stored_checksums[i]) {
                continue;
            }
        }

        mram_read_bytes(MRAM_FLASH_BASE_ADDRESS + i * FLASH_BLOCK_SIZE, mram_block, FLASH_BLOCK_SIZE);

        stored_checksums[i] = crc32(mram_block, FLASH_BLOCK_SIZE);

        bool any_error = false;
        for (uint32_t j = 0; j < FLASH_BLOCK_SIZE; j++) {
            if (mram_block[j] != flash_block[j]) {
                any_error = true;
                break;
            }
        }

        if (any_error) {
            watchdog_pet();

            flash_erase(&FLASH_0, i * FLASH_BLOCK_SIZE, 1);
            flash_write(&FLASH_0, i * FLASH_BLOCK_SIZE, mram_block, FLASH_BLOCK_SIZE);

            watchdog_pet();
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