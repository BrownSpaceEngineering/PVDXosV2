/**
 * main.c
 *
 * The main entry point for PVDXos. This file initializes the hardware, verifies that the bootloader executed
 * successfully, creates the high-level OS integrity tasks along with the Cosmic Monkey task, and finally starts
 * the FreeRTOS scheduler.
 *
 * Created: November 20, 2023
 * Authors: Oren Kohavi, Siddharta Laloux, Tanish Makadia, Yi Liu, Defne Doken, Aidan Wang, Ignacio Blancas Rodriguez
 */

#include "main.h"

cosmic_monkey_task_arguments_t cm_args = {0};

static status_t PVDX_init(void) {
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
    
    /* Initialize RTC for camera timestamp functionality */
    if (rtc_init() != SUCCESS) {
        fatal("Failed to initialize RTC\n");
    }
    info_impl("RTC initialized successfully\n");
    
    /* Test RTC functionality */
    if (rtc_test() != SUCCESS) {
        warning("RTC test failed, but continuing...\n");
    }
    
    // info_impl(RTT_CTRL_RESET RTT_CTRL_CLEAR); // Reset the terminal
    info_impl("--- Atmel & Hardware Initialization Complete ---\n");
    info_impl("[+] Build Type: %s\n", BUILD_TYPE);
    info_impl("[+] Build Date: %s\n", BUILD_DATE);
    info_impl("[+] Build Time: %s\n", BUILD_TIME);
    info_impl("[+] Built from branch: %s\n", GIT_BRANCH_NAME);
    info_impl("[+] Built from commit: %s\n", GIT_COMMIT_HASH);

    // Bootloader sets a magic number in backup RAM to indicate that it has run successfully
    uint32_t *p_magic_number = (uint32_t *)BOOTLOADER_MAGIC_NUMBER_ADDRESS;
    uint32_t magic_number = *p_magic_number;
    *p_magic_number = 0; // Clear the magic number so that this value doesn't linger
    if (magic_number == BOOTLOADER_MAGIC_NUMBER_VALUE) {
        info_impl("[+] Bootloader executed normally\n");
    } else {
        warning_impl("[!] Abnormal bootloader behavior (Magic Number: %x)\n", magic_number);
    }

    /* ---------- INIT WATCHDOG, COMMAND_DISPATCHER, TASK_MANAGER TASKS (in that order) ---------- */

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

    /* ---------- COSMIC MONKEY TASK ---------- */

    #if defined(UNITTEST) || defined(DEVBUILD)
        #if defined(UNITTEST)
        cm_args.frequency = 10;
        #endif
        #if defined(DEVBUILD)
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
