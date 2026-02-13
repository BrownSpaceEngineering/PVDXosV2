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
#include "mram.h"
#include "reflash.h"

#define RSTC_RCAUSE (0x40000C00UL)  // Reset Cause Register

#include "checks/device_checks.h"
#include "globals.h"
#include "logging.h"
#include "tests/test.h"

#include "linalg_task.h"


void debug_matrix(double* A, int row, int column) {
	for(int i = 0; i < row; i++){
		for(int j = 0; j < column; j++){
			debug("%0.18f ", *(A++));
		}
		debug("\n");
	}
	debug("\n");

}

cosmic_monkey_task_arguments_t cm_args = {0};

void init_logging(void) {
    // Segger Buffer 0 is pre-configured at compile time according to segger documentation
    // Config the logging output channel (assuming it's not zero)
    if (LOGGING_RTT_OUTPUT_CHANNEL != 0) {
        SEGGER_RTT_ConfigUpBuffer(LOGGING_RTT_OUTPUT_CHANNEL, "Log Output", SEGGER_RTT_LOG_BUFFER, SEGGER_RTT_LOG_BUFFER_SIZE,
                                  SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    }
}

void report_reset_cause(void) {
    uint8_t cause = *((uint8_t *)RSTC_RCAUSE);

    info_impl("[+] Last reset cause(s):\n");
    if (cause & 0x01) {
        info_impl("- Power on Reset\n");
    }
    if (cause & 0x02) {
        info_impl("- Brown-Out Detector Core Reset\n");
    }
    if (cause & 0x04) {
        info_impl("- Brown-Out Detector VDD Reset\n");
    }
    if (cause & 0x10) {
        info_impl("- NVM Reset\n");
    }
    if (cause & 0x20) {
        info_impl("- External Reset\n");
    }
    if (cause & 0x40) {
        info_impl("- Watchdog Reset\n");
    }
    if (cause & 0x80) {
        info_impl("- System Reset Request\n");
    }
}

int main(void) {
    /* ---------- HARDWARE & LOGGING INITIALIZATION ---------- */

    /* Initializes MCU, drivers and middleware */
    atmel_start_init();
    init_logging();
    // info_impl(RTT_CTRL_RESET RTT_CTRL_CLEAR); // Reset the terminal
    info_impl("--- Atmel & Hardware Initialization Complete ---\n");
    info_impl("[+] Build Type: %s\n", BUILD_TYPE);
    info_impl("[+] Build Date: %s\n", BUILD_DATE);
    info_impl("[+] Build Time: %s\n", BUILD_TIME);
    info_impl("[+] Built from branch: %s\n", GIT_BRANCH_NAME);
    info_impl("[+] Built from commit: %s\n", GIT_COMMIT_HASH);
    report_reset_cause();

    reflash_bootloaders();

    /* ---------- INIT WATCHDOG, COMMAND_DISPATCHER, TASK_MANAGER TASKS (in that order) ---------- */

    info("AT_LEAST_ONE_DEVICE_FAILED: %d\n", check_all_devices_on_startup());

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

    double A[4] = {1., 2., 3., 4.}; 
    double B[4] = {5., 6., 7., 8.}; 
    double C[4] = {0.}; 

    mul(A, B, false, C, 2, 2, 2); 
    debug_matrix(C, 2, 2); 

    /* ---------- COSMIC MONKEY TASK ---------- */

// #if defined(UNITTEST) || defined(DEVBUILD)
//     #if defined(UNITTEST)
//     cm_args.frequency = 10;
//     #endif
//     #if defined(DEVBUILD)
//     cm_args.frequency = 1; // Bitflips per second
//     #endif

//     TaskHandle_t cosmic_monkey_task_handle =
//         xTaskCreateStatic(main_cosmic_monkey, "CosmicMonkey", COSMIC_MONKEY_TASK_STACK_SIZE, (void *)&cm_args, 1,
//                           cosmic_monkey_mem.cosmic_monkey_task_stack, &cosmic_monkey_mem.cosmic_monkey_task_tcb);
//     if (cosmic_monkey_task_handle == NULL) {
//         warning("Cosmic Monkey Task Creation Failed!\n");
//     } else {
//         info("Cosmic Monkey Task initialized\n");
//     }
// #endif // Cosmic Monkey

    /* ---------- START FREERTOS SCHEDULER ---------- */

    // Start the scheduler
    vTaskStartScheduler();
    fatal("vTaskStartScheduler Returned! -- Should never happen!\n");
}
