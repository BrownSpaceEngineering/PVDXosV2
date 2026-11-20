#include "reflash_task.h"

#include <string.h>

#include "drivers/mram/mram_driver.h"
#include "flash_driver.h"
#include "logging.h"
#include "task_list.h"
#include "watchdog_task.h"

#define REFLASH_INTERVAL_TICKS ((TickType_t)(6u * 60u * 60u) * configTICK_RATE_HZ)
#define BOOTLOADER_CRC_SIZE (FLASH_STAGE2_SIZE - sizeof(uint32_t))

reflash_task_memory_t reflash_mem;
static uint8_t slot_buffer[FLASH_STAGE2_SIZE] __attribute__((aligned(4)));
static bool reflash_in_progress;

static bool slot_crc_valid(void) {
    uint32_t stored_crc;
    memcpy(&stored_crc, slot_buffer + BOOTLOADER_CRC_SIZE, sizeof(stored_crc));
    return crc32(slot_buffer, BOOTLOADER_CRC_SIZE) == stored_crc;
}

static void reflash_checkin(bool sched_running) {
    if (sched_running) {
        pvdx_task_t *current = get_current_task();
        if (current != NULL && current->has_registered) {
            watchdog_checkin(current);
        }
    }
}

// MRAM/SPI initialization is owned by OS startup; this task only uses the read API.
void reflash_bootloaders(bool sched_running) {
    if (sched_running) {
        taskENTER_CRITICAL();
    }
    bool already_running = reflash_in_progress;
    reflash_in_progress = true;
    if (sched_running) {
        taskEXIT_CRITICAL();
    }
    if (already_running) {
        return;
    }

    // Slot 0 is preboot; slots 1 through 8 are individually linked stage-2 copies.
    for (uint32_t index = 0; index <= FLASH_STAGE2_COUNT; index++) {
        uint32_t address = FLASH_STAGE1_ADDR + index * FLASH_STAGE2_SIZE;
        reflash_checkin(sched_running);
        bool no_ecc_error = flash_read_bootloader_block(address, slot_buffer);
        if (no_ecc_error && slot_crc_valid()) {
            continue;
        }
        warning("reflash: slot %u has %s; checking MRAM\n", (unsigned int)index, no_ecc_error ? "bad CRC" : "ECC error");
        uint32_t mram_address = MRAM_BOOTLOADER_ADDR + address - FLASH_STAGE1_ADDR;
        if (mram_read_bytes(mram_address, slot_buffer, sizeof(slot_buffer), NULL)) {
            warning("reflash: slot %u MRAM read failed; flash unchanged\n", (unsigned int)index);
            continue;
        }
        if (!slot_crc_valid()) {
            warning("reflash: slot %u MRAM CRC failed; flash unchanged\n", (unsigned int)index);
            continue;
        }

        // NVM commands/cache settings are shared. IRQs and the hardware watchdog remain active.
        if (sched_running) {
            vTaskSuspendAll();
        }
        bool programmed = flash_program_bootloader_block(address, slot_buffer);
        if (sched_running) {
            (void)xTaskResumeAll();
        }
        if (!programmed || !flash_read_bootloader_block(address, slot_buffer) || !slot_crc_valid()) {
            warning("reflash: slot %u repair failed\n", (unsigned int)index);
        } else {
            info("reflash: slot %u repaired and verified\n", (unsigned int)index);
        }
        reflash_checkin(sched_running);
        if (sched_running) {
            vTaskDelay(1);
        }
    }
    if (sched_running) {
        taskENTER_CRITICAL();
    }
    reflash_in_progress = false;
    if (sched_running) {
        taskEXIT_CRITICAL();
    }
}

void main_reflash_task(void *pvParameters) {
    (void)pvParameters;
    reflash_bootloaders(true);
    TickType_t last_scan = xTaskGetTickCount();
    while (true) {
        // Wake regularly for watchdog check-ins, while scanning only every six hours.
        vTaskDelay(pdMS_TO_TICKS(1000));
        reflash_checkin(true);
        TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - last_scan) >= REFLASH_INTERVAL_TICKS) {
            reflash_bootloaders(true);
            last_scan = xTaskGetTickCount();
        }
    }
}
