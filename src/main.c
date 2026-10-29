/**
 * main.c
 *
 * The main entry point for PVDXos. This file initializes the hardware, verifies that the bootloader executed
 * successfully and runs the startup device checks. In a normal build it then creates the high-level OS integrity
 * tasks along with the Cosmic Monkey task, and finally starts the FreeRTOS scheduler. In a BURN_SEQUENCE build it
 * instead waits 30 minutes, turns on the burn pin, then two minutes apart sends UHF beacons, shows the PVDX logo on
 * the OLED and streams a camera picture over RTT, and idles forever without ever starting the scheduler.
 *
 * Created: November 20, 2023
 * Modified: October 28, 2026
 * Authors: Oren Kohavi, Siddharta Laloux, Tanish Makadia, Yi Liu,
 *          Defne Doken, Aidan Wang, Ignacio Blancas Rodriguez, Alexander Thaep
 */

#include "main.h"

#include "SEGGER_RTT.h"
#include "checks/device_checks.h"
#include "globals.h" // defines BURN_SEQUENCE
#include "logging.h"

#ifdef BURN_SEQUENCE
    #include "arducam_driver.h"
    #include "display_driver.h"
    #include "burn/burn_uhf.h"
    #include "image_buffer_PVDX.h"
    #include "watchdog_driver.h"
#else
    #include "ccsds/ccsds.h"
    #include "cosmic_monkey_task.h"
    #include "rtos_start.h"
    #include "task_list.h"
    #include "tasks/task_manager/task_manager_task.h"
    #include "tests/test.h"
#endif

#ifdef BURN_SEQUENCE
    #define BURN_WAIT_MINUTES      30
    #define POST_BURN_STEP_MINUTES 2 // wait before each post-burn step

    #define CAMERA_CAPTURE_ATTEMPTS 3
#else
cosmic_monkey_task_arguments_t cm_args = {0};
#endif

static status_t PVDX_init(void) {
    // Segger Buffer 0 is pre-configured at compile time according to segger documentation
    // Config the logging output channel (assuming it's not zero)
    if (LOGGING_RTT_OUTPUT_CHANNEL != 0) {
        SEGGER_RTT_ConfigUpBuffer(LOGGING_RTT_OUTPUT_CHANNEL, "Log Output", SEGGER_RTT_LOG_BUFFER, SEGGER_RTT_LOG_BUFFER_SIZE,
                                  SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    }
    return SUCCESS;
}

#ifdef BURN_SEQUENCE
// Busy-waits one second at a time (delay_ms is fine here: the scheduler never runs, so nothing else owns SysTick).
// The watchdog task never runs either, so the hardware watchdog is normally off. It is fed every second anyway in
// case it is still running from an earlier boot.
static void wait_seconds_feeding_watchdog(uint32_t seconds) {
    for (uint32_t s = 0; s < seconds; s++) {
        watchdog_feed(WDT);
        delay_ms(1000);
    }
}

static void wait_minutes_before(uint32_t minutes, const char *next_step) {
    info("Waiting %lu minutes before %s\n", (unsigned long)minutes, next_step);
    for (uint32_t minute = 0; minute < minutes; minute++) {
        wait_seconds_feeding_watchdog(60);
        info("%lu/%lu minutes elapsed\n", (unsigned long)(minute + 1), (unsigned long)minutes);
    }
}

/* ---------- STEP 2: OLED ---------- */

static void show_logo_on_display(void) {
    if (init_display_hardware() != SUCCESS) {
        warning("display: init failed, skipping OLED\n");
        return;
    }
    display_set_buffer(IMAGE_BUFFER_PVDX);
    if (display_update() != SUCCESS) {
        warning("display: update failed\n");
        return;
    }
    info("display: showing PVDX logo\n");
}

/* ---------- STEP 3: CAMERA PICTURE OVER RTT ---------- */

static uint8_t camera_rtt_image_buffer[SEGGER_RTT_IMAGE_BUFFER_SIZE];

// Called before the device checks, so that the camera check can capture too (BURN_SEQUENCE_CAMERA_CHECK_CAPTURE)
static void configure_camera_rtt_channel(void) {
    #if defined(RELEASE)
    // No host drains channel 2 in flight, so drop image bytes that don't fit rather than hang
    const unsigned rtt_image_mode = SEGGER_RTT_MODE_NO_BLOCK_SKIP;
    #else
    // Block so image bytes are not dropped. NOTE: this hangs until a host reads RTT channel 2
    const unsigned rtt_image_mode = SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL;
    #endif
    SEGGER_RTT_ConfigUpBuffer(CAMERA_RTT_OUTPUT_CHANNEL, "Image", camera_rtt_image_buffer, SEGGER_RTT_IMAGE_BUFFER_SIZE,
                              rtt_image_mode);
}

static void send_picture_over_rtt(void) {
    for (uint32_t attempt = 1; attempt <= CAMERA_CAPTURE_ATTEMPTS; attempt++) {
        watchdog_feed(WDT);
        status_t status = init_arducam_hardware();
        if (status == SUCCESS) {
            status = capture_rtt();
        }
        if (status == SUCCESS) {
            info("camera: picture sent over RTT channel %d\n", CAMERA_RTT_OUTPUT_CHANNEL);
            return;
        }
        warning("camera: attempt %lu/%d failed (status=%d)\n", (unsigned long)attempt, CAMERA_CAPTURE_ATTEMPTS, status);
    }
    warning("camera: giving up on the picture\n");
}
#endif

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

    // Bootloader sets a magic number in backup RAM to indicate that it has run successfully
    uint32_t *p_magic_number = (uint32_t *)BOOTLOADER_MAGIC_NUMBER_ADDRESS;
    uint32_t magic_number = *p_magic_number;
    *p_magic_number = 0; // Clear the magic number so that this value doesn't linger
    if (magic_number == BOOTLOADER_MAGIC_NUMBER_VALUE) {
        info_impl("[+] Bootloader executed normally\n");
    } else {
        warning_impl("[!] Abnormal bootloader behavior (Magic Number: %x)\n", magic_number);
    }

    /* ---------- DEVICE CHECKS ---------- */

#ifdef BURN_SEQUENCE
    configure_camera_rtt_channel();
#endif

    // Called outside info(): in RELEASE builds info() compiles out, which would skip the checks entirely
    bool at_least_one_failed = check_all_devices_on_startup();
    info("AT_LEAST_ONE_DEVICE_FAILED: %d\n", at_least_one_failed);
    (void)at_least_one_failed; // unused when info() compiles out

#ifdef BURN_SEQUENCE
    /* ---------- WAIT, THEN BURN ---------- */

    wait_minutes_before(BURN_WAIT_MINUTES, "turning on the burn pin");

    gpio_set_pin_level(BURN, true);
    info("Burn pin (PB29) on\n");

    /* ---------- POST-BURN STEPS, TWO MINUTES APART ---------- */

    #if BURN_SEQUENCE_UHF_MODE == BURN_UHF_MODE_LINK
    wait_minutes_before(POST_BURN_STEP_MINUTES, "the UHF tx/rx link");
    burn_uhf_link();
    #else
    wait_minutes_before(POST_BURN_STEP_MINUTES, "UHF beacons");
    burn_uhf_beacon();
    #endif

    wait_minutes_before(POST_BURN_STEP_MINUTES, "showing the PVDX logo on the OLED");
    show_logo_on_display();

    wait_minutes_before(POST_BURN_STEP_MINUTES, "sending a camera picture over RTT");
    send_picture_over_rtt();

    /* ---------- IDLE FOREVER (scheduler is never started) ---------- */

    while (true) {
        wait_seconds_feeding_watchdog(1);
    }
#else
    /* ---------- INIT WATCHDOG, COMMAND_DISPATCHER, TASK_MANAGER TASKS (in that order) ---------- */

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
#endif // BURN_SEQUENCE
}
