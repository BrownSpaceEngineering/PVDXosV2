#include "reflash_task.h"

#include <string.h>

#include "cmd_dispatcher.h"
#include "flash_driver.h"
#include "logging.h"
#include "mram_map.h"
#include "state_machine.h"
#include "task_list.h"
#include "watchdog_task.h"

#define BOOTLOADER_CRC_SIZE (FLASH_STAGE2_SIZE - sizeof(uint32_t))

reflash_task_memory_t reflash_mem;
static uint8_t slot_buffer[FLASH_STAGE2_SIZE] __attribute__((aligned(4)));
// Static lifetime: the task manager reads this after the command is queued.
static input_update_data_t reflash_inputs = {
    .input_update_bitmask = INPUT_UPDATE_BIT(REFLASH_UPDATE),
};

static bool slot_crc_valid(void) {
    uint32_t stored_crc;
    memcpy(&stored_crc, slot_buffer + BOOTLOADER_CRC_SIZE, sizeof(stored_crc));
    return crc32(slot_buffer, BOOTLOADER_CRC_SIZE) == stored_crc;
}

void reflash_checkin(void) {
    pvdx_task_t *current = get_current_task();
    if (current != NULL && current->has_registered) {
        watchdog_checkin(current);
    }
}

// Only this task calls the scan; MRAM/SPI initialization belongs to OS startup.
status_t reflash_bootloaders(void) {
    status_t result = SUCCESS;
    // Slot 0 is preboot; slots 1 through 8 are individually linked stage-2 copies.
    for (uint32_t index = 0; index <= FLASH_STAGE2_COUNT; index++) {
        uint32_t address = FLASH_STAGE1_ADDR + index * FLASH_STAGE2_SIZE;
        reflash_checkin();
        bool no_ecc_error = flash_read_bootloader_block(address, slot_buffer);
        if (no_ecc_error && slot_crc_valid()) {
            continue;
        }
        warning("reflash: slot %u has %s; checking MRAM\n", (unsigned int)index, no_ecc_error ? "bad CRC" : "ECC error");
        if (!mram_bootloader_copy_read(address - FLASH_STAGE1_ADDR, slot_buffer, sizeof(slot_buffer))) {
            warning("reflash: slot %u MRAM read failed; flash unchanged\n", (unsigned int)index);
            result = ERROR_READ_FAILED;
            continue;
        }
        if (!slot_crc_valid()) {
            warning("reflash: slot %u MRAM CRC failed; flash unchanged\n", (unsigned int)index);
            result = ERROR_READ_FAILED;
            continue;
        }

        // NVM commands/cache settings are shared. IRQs and the hardware watchdog remain active.
        vTaskSuspendAll();
        bool programmed = flash_program_bootloader_block(address, slot_buffer);
        (void)xTaskResumeAll();
        if (!programmed || !flash_read_bootloader_block(address, slot_buffer) || !slot_crc_valid()) {
            warning("reflash: slot %u repair failed\n", (unsigned int)index);
            result = ERROR_WRITE_FAILED;
        } else {
            info("reflash: slot %u repaired and verified\n", (unsigned int)index);
        }
        reflash_checkin();
        vTaskDelay(1);
    }
    return result;
}

// The task manager calls this before every enable/resume. One notification
// releases one scan, even if the previous completion has not returned yet.
void reflash_prepare_resume(void) {
    xTaskNotifyGive(p_reflash_task->handle);
}

void reflash_report_completion(void) {
    // Completion means the scan finished, including any logged repair failures.
    reflash_inputs.new_values.reflash_update_pending = false;
    command_t update = get_state_input_update_command(&reflash_inputs);
    const status_t status = enqueue_command(&update);
    if (status != SUCCESS) {
        warning("reflash: Failed to enqueue completion update (status %d)\n", status);
    }
}
