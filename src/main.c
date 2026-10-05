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
#include "reflash_task.h"

#define RSTC_RCAUSE (0x40000C00UL)  // Reset Cause Register

#include "SEGGER_RTT.h"
#include "checks/device_checks.h"
#include "cosmic_monkey_task.h"
#include "driver_init.h"
#include "globals.h"
#include "logging.h"
#include "rtos_start.h"
#include "task_list.h"
#include "tests/test.h"

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

// Standalone MRAM smoke test: read the Read-ID (0x9F) of each of the 3 MRAMs over
// SPI_MRAM and log it. A good EM008LX returns 6B BB 14. Runs at OS startup; does
// not use the mram_driver (which fatal-hangs on a bad ID).
static void test_mram(void) {
    const uint32_t cs_pins[3] = {MRAM1_CS, MRAM2_CS, MRAM3_CS};

    spi_m_sync_enable(&SPI_MRAM);
    gpio_set_pin_level(MRAM1_CS, true);
    gpio_set_pin_level(MRAM2_CS, true);
    gpio_set_pin_level(MRAM3_CS, true);
    delay_ms(50);

    for (int i = 0; i < 3; i++) {
        // Mirror the driver's check_device_id exactly: write 0x9F, then read 3 bytes,
        // as two transfers with CS held low across both (same NULL-buffer pattern as
        // the driver's spi_write()/spi_read()).
        uint8_t cmd = 0x9F;
        uint8_t id[3] = {0};
        struct spi_xfer wr = {.txbuf = &cmd, .rxbuf = NULL, .size = 1};
        struct spi_xfer rd = {.txbuf = NULL, .rxbuf = id, .size = 3};

        gpio_set_pin_level(cs_pins[i], false);
        int32_t rc_w = spi_m_sync_transfer(&SPI_MRAM, &wr);
        int32_t rc_r = spi_m_sync_transfer(&SPI_MRAM, &rd);
        gpio_set_pin_level(cs_pins[i], true);

        info("[MRAM TEST] MRAM%d ID = %02x %02x %02x (expect 6B BB 14) [rc %ld/%ld]\n", i + 1, id[0], id[1], id[2],
             (long)rc_w, (long)rc_r);
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

    // Standalone MRAM ID test (runs before anything that touches MRAM).
    test_mram();

    reflash_bootloaders(false);

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
