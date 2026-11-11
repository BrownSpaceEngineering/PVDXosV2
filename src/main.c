/**
 * main.c
 *
 * The main entry point for PVDXos. This file initializes the hardware, verifies that the bootloader executed
 * successfully, creates the high-level OS integrity tasks along with the Cosmic Monkey task, and finally starts
 * the FreeRTOS scheduler.
 *
 * Created: November 20, 2023
 * Modified: November 9, 2025
 * Authors: Oren Kohavi, Siddharta Laloux, Tanish Makadia, Yi Liu,
 *          Defne Doken, Aidan Wang, Ignacio Blancas Rodriguez, Alexander Thaep
 */

#include "main.h"

#include "SEGGER_RTT.h"
#include "boot_record.h"
#include "ccsds/ccsds.h"
#include "checks/device_checks.h"
#include "cosmic_monkey_task.h"
#include "globals.h"
#include "logging.h"
#include "rtos_start.h"
#include "task_list.h"
#include "tasks/task_manager/task_manager_task.h"
#include "tests/test.h"

cosmic_monkey_task_arguments_t cm_args = {0};

__attribute__((unused)) static const char *mram_state_name(uint8_t state) { // Unused when logging compiles out

    static const char *const names[] = {"off", "ok", "degraded", "failed"};
    return names[state & 0x03u];
}

// Logs what stage 1 and stage 2 did this boot (bootloader/src/boot_record.h), then invalidates the record.
static void log_boot_record(void) {
    volatile boot_record_t *record = (volatile boot_record_t *)BOOT_RECORD_ADDR;
    if (record->magic != BOOT_RECORD_MAGIC) {
        warning("Boot: no boot record from stage 1/2 (magic %x)\n", (unsigned int)record->magic);
        return;
    }

    if (record->stage2_slot == BOOT_STAGE2_SLOT_FALLBACK) {
        warning("Boot: all stage-2 copies failed their CRC; stage 1 started the OS directly (no MRAM repair)\n");
    } else if (record->stage2_slot == BOOT_STAGE2_SLOT_UNKNOWN) {
        info("Boot: stage 2 entered without stage 1\n");
    } else {
        info("Boot: stage 2 copy %u\n", (unsigned int)record->stage2_slot);
    }

    if (record->stage2_slot != BOOT_STAGE2_SLOT_FALLBACK) {
        info("Boot: MRAM modules at init %s/%s/%s, at handoff %s/%s/%s\n", mram_state_name(record->mram_status_init),
             mram_state_name(record->mram_status_init >> 2), mram_state_name(record->mram_status_init >> 4),
             mram_state_name(record->mram_status_final), mram_state_name(record->mram_status_final >> 2),
             mram_state_name(record->mram_status_final >> 4));

        switch (record->refresh) {
            case BOOT_REFRESH_NOT_REQUESTED:
                break;
            case BOOT_REFRESH_OK:
                info("Boot: MRAM refresh complete (%u blocks copied)\n", (unsigned int)record->refresh_blocks);
                break;
            case BOOT_REFRESH_NO_MRAM:
                warning("Boot: MRAM refresh requested but no MRAM module is healthy; will retry next boot\n");
                break;
            case BOOT_REFRESH_BAD_FLASH:
                warning("Boot: MRAM refresh refused: flash image fails its own header/CRCs; will retry next boot\n");
                break;
            default:
                warning("Boot: MRAM refresh FAILED at MRAM %x after %u blocks; golden copy invalid, will retry next boot\n",
                        (unsigned int)record->refresh_fail_addr, (unsigned int)record->refresh_blocks);
                break;
        }

        switch (record->repair) {
            case BOOT_REPAIR_DONE:
                info("Boot: flash checked against MRAM: %u blocks, %u repaired, %u with unusable MRAM copy, %u program "
                     "failures%s\n",
                     (unsigned int)record->blocks_checked, (unsigned int)record->blocks_repaired,
                     (unsigned int)record->blocks_bad_mram, (unsigned int)record->blocks_program_failed,
                     record->header_rewritten ? ", header row rewritten" : "");
                break;
            case BOOT_REPAIR_NO_MRAM:
                warning("Boot: flash repair skipped: no healthy MRAM module\n");
                break;
            case BOOT_REPAIR_HEADER_READ_FAILED:
                warning("Boot: flash repair skipped: MRAM header read failed\n");
                break;
            case BOOT_REPAIR_NO_IMAGE:
                warning("Boot: flash repair skipped: MRAM holds no valid golden image\n");
                break;
            default:
                warning("Boot: stage 2 did not record a repair result\n");
                break;
        }
    }
    record->magic = 0u;
}

static status_t PVDX_init(void) {
    // The RTT control block is pinned to the start of RAM and shared with stage 2, so it is not zeroed at startup:
    // re-create it before anything logs (otherwise RTT would keep using stage 2's buffer pointers).
    SEGGER_RTT_Init();
    // Segger Buffer 0 is pre-configured at compile time according to segger documentation
    // Config the logging output channel (assuming it's not zero)
    if (LOGGING_RTT_OUTPUT_CHANNEL != 0) {
        SEGGER_RTT_ConfigUpBuffer(LOGGING_RTT_OUTPUT_CHANNEL, "Log Output", SEGGER_RTT_LOG_BUFFER, SEGGER_RTT_LOG_BUFFER_SIZE,
                                  SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    }
    return SUCCESS;
}

int main(void) {
    /* ---------- HARDWARE & LOGGING INITIALIZATION + BOOTLOADER CHECK ---------- */

    /* Initializes MCU, drivers and middleware */
    atmel_start_init();
    PVDX_init();
    // info_impl(RTT_CTRL_RESET RTT_CTRL_CLEAR); // Reset the terminal
    info("--- Atmel & Hardware Initialization Complete ---\n");
    info("[+] Build Type: %s\n", BUILD_TYPE);
    info("[+] Build Date: %s\n", BUILD_DATE);
    info("[+] Build Time: %s\n", BUILD_TIME);
    info("[+] Built from branch: %s\n", GIT_BRANCH_NAME);
    info("[+] Built from commit: %s\n", GIT_COMMIT_HASH);

    log_boot_record();

    /* ---------- INIT WATCHDOG, COMMAND_DISPATCHER, TASK_MANAGER TASKS (in that order) ---------- */

    info("AT_LEAST_ONE_DEVICE_FAILED: %d\n", check_all_devices_on_startup());

    ccsds_init();

/* -------------------------------------- TESTS ---------------------------------------------- */
#ifdef UNITTEST
    tests_run();
#endif

    // Initialize a mutex wrapping the shared PVDX task list struct
    task_list_mutex = xSemaphoreCreateMutexStatic(&task_list_mutex_buffer);

    if (task_list_mutex == NULL) {
        fatal("Failed to create PVDX task list mutex");
    }

    if (task_list[0] != p_watchdog_task) {
        fatal("Watchdog is not first in task_list!");
    }

    // Initialize all OS integrity tasks
    for (pvdx_task_t **curr_task = task_list; *curr_task != NULL; curr_task++) {
        if ((*curr_task)->task_type == OS) {
            init_task_pointer(*curr_task);
            info("%s initialized\n", (*curr_task)->name);
        }
    }

    // COMMENTED OUT because this is now controlled by the task manager
    // Initialize all sensor integrity tasks
    // for (pvdx_task_t **curr_task = task_list; *curr_task != NULL; curr_task++) {
    //     if ((*curr_task)->task_type == SENSOR) {
    //         init_task_pointer(*curr_task);
    //         info("%s initialized\n", (*curr_task)->name);
    //     }
    // }

    /* ---------- COSMIC MONKEY TASK ---------- */

#if defined(UNITTEST) || defined(DEVBUILD)
    #if defined(UNITTEST)
    cm_args.frequency = 1;
    #endif
    #if defined(DEVBUILD) // set to 0 for now for ease of debugging
    cm_args.frequency = 0; // Bitflips per second
    #endif

    TaskHandle_t cosmic_monkey_task_handle =
        xTaskCreateStatic(main_cosmic_monkey, "CosmicMonkey", COSMIC_MONKEY_TASK_STACK_SIZE, (void *)&cm_args, 1,
                          cosmic_monkey_mem.cosmic_monkey_task_stack, &cosmic_monkey_mem.cosmic_monkey_task_tcb);
    if (cosmic_monkey_task_handle == NULL) {
        warning("Cosmic Monkey Task Creation Failed!\n");
    } else {
        info("Cosmic Monkey Task initialized\n");
    }
#endif // Cosmic Monkey

    /* ---------- START FREERTOS SCHEDULER ---------- */

    // Start the scheduler
    vTaskStartScheduler();
    fatal("vTaskStartScheduler Returned! -- Should never happen!\n");
}
