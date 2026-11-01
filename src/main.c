/**
 * main.c
 *
 * The main entry point for PVDXos. This file initializes the hardware, verifies that the bootloader executed
 * successfully and runs the startup device checks. In a normal build it then creates the high-level OS integrity
 * tasks along with the Cosmic Monkey task, and finally starts the FreeRTOS scheduler. In a BURN_SEQUENCE build it
 * instead never burns (the burn pin stays low): two minutes apart it sends UHF beacons, shows the PVDX logo on the OLED
 * and streams a camera picture over RTT, and idles forever without ever starting the scheduler.
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
#include "mutexes.h"
#include "arducam_driver.h"
#include "display_driver.h"
#include "burn/burn_uhf.h"
#include "image_buffer_PVDX.h"
#include "mram_driver.h"
#include "watchdog_driver.h"
#include "ccsds/ccsds.h"
#include "cosmic_monkey_task.h"
#include "rtos_start.h"
#include "task_list.h"
#include "tasks/task_manager/task_manager_task.h"
#include "tests/test.h"


#define BURN_WAIT_MINUTES      30
#define BURN_ON_FOR_SECONDS 30
#define POST_BURN_STEP_MINUTES 2 // wait before each post-burn step

#define CAMERA_CAPTURE_ATTEMPTS 3
cosmic_monkey_task_arguments_t cm_args = {0};

// The burn record lives in the last 256-byte page of the 1 MB MRAM, clear of the development flash mirror
// (MRAM_FLASH_BASE_ADDRESS..+MRAM_FLASH_SIZE) and the MRAM hardware test region. mram_read_bytes() only reads whole
// pages, so the full page is read and the record is its first bytes.
#define BURN_RECORD_MRAM_ADDRESS 0x000FFF00u
#define BURN_RECORD_PAGE_SIZE    256u
// Marks the record as written by this code, so never-written MRAM starts from a fresh record instead of garbage
#define BURN_RECORD_MAGIC        0xB12EC0DEu
// How often the burn wait saves its progress to MRAM, so a reboot resumes the wait instead of restarting it
#define BURN_RECORD_SAVE_PERIOD_S 60u


static status_t PVDX_init(void) {
    // Segger Buffer 0 is pre-configured at compile time according to segger documentation
    // Config the logging output channel (assuming it's not zero)
    if (LOGGING_RTT_OUTPUT_CHANNEL != 0) {
        SEGGER_RTT_ConfigUpBuffer(LOGGING_RTT_OUTPUT_CHANNEL, "Log Output", SEGGER_RTT_LOG_BUFFER, SEGGER_RTT_LOG_BUFFER_SIZE,
                                  SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    }
    return SUCCESS;
}

// Busy-waits one second at a time (delay_ms is fine here: the scheduler never runs, so nothing else owns SysTick).
// The watchdog task never runs either, so the hardware watchdog is normally off. It is fed every second anyway in
// case it is still running from an earlier boot.
[[maybe_unused]] static void wait_seconds_feeding_watchdog(uint32_t seconds) {
    for (uint32_t s = 0; s < seconds; s++) {
        watchdog_feed(WDT);
        delay_ms(1000);
    }
}

[[maybe_unused]] static void wait_minutes_before(uint32_t minutes, const char *next_step) {
    info("Waiting %lu minutes before %s\n", (unsigned long)minutes, next_step);
    for (uint32_t minute = 0; minute < minutes; minute++) {
        wait_seconds_feeding_watchdog(60);
        info("%lu/%lu minutes elapsed\n", (unsigned long)(minute + 1), (unsigned long)minutes);
    }
}

/* ---------- STEP 2: OLED ---------- */

[[maybe_unused]] static void show_logo_on_display(void) {
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
[[maybe_unused]] static void configure_camera_rtt_channel(void) {
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

[[maybe_unused]] static void send_picture_over_rtt(void) {
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

/* ---------- BURN WIRE STATE IN MRAM ---------- */

// Persists across reboots
typedef struct {
    uint32_t magic;
    uint32_t boot_count;     // boots seen since the record was created
    uint32_t burned;         // nonzero once the burn wire has been fired
    uint32_t burn_attempts;  // incremented just before each burn, so a reboot mid-burn is visible
    uint32_t elapsed_wait_s; // seconds of the pre-burn wait completed so far
} burn_record_t;

// Reads the record (majority-voted across the three MRAM modules). A missing record is replaced by a fresh one,
// which means the full wait runs again: waiting too long is safer than burning too early.
static burn_record_t load_burn_record(void) {
    // uint32_t so that the page is word-aligned and can be read as a burn_record_t
    static uint32_t page[BURN_RECORD_PAGE_SIZE / sizeof(uint32_t)];
    mram_read_bytes(BURN_RECORD_MRAM_ADDRESS, (uint8_t *)page, BURN_RECORD_PAGE_SIZE);

    burn_record_t record = *(const burn_record_t *)page;
    if (record.magic != BURN_RECORD_MAGIC) {
        warning_impl("[!] No burn record in MRAM (magic: %lx); starting a fresh one\n", (unsigned long)record.magic);
        record = (burn_record_t){.magic = BURN_RECORD_MAGIC};
    }
    return record;
}

// Writes the record to all three MRAM modules
static void save_burn_record(const burn_record_t *const p_record) {
    mram_write_bytes(BURN_RECORD_MRAM_ADDRESS, (const uint8_t *)p_record, sizeof(*p_record));
}

#ifdef TEST_SBAND_ONLY_IMAGE
// S-band-only test image (make test TESTS=stxg2 or stxg2_sweep). Skips the MRAM burn record, the burn wire, the startup
// device checks, UHF and all tasks, so no other device is touched. atmel_start_init() leaves the burn pin, heaters,
// magnetorquer drivers and S-band enable low. The S-band test runs the STXG2 POST and transmits only if it passes.
// When the test stops, the MCU idles here. The scheduler never starts.
[[noreturn]] static void sband_only_image_main(void) {
    info_impl("[+] S-band-only test image: no burn, no device checks, no tasks. Only the STXG2 is powered.\n");
    tests_run();
    info_impl("[+] S-band test stopped. Idling. Turn off the power or flash another image.\n");
    while (true) {
        wait_seconds_feeding_watchdog(1);
    }
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

#ifdef TEST_SBAND_ONLY_IMAGE
    sband_only_image_main(); // Never returns
#endif


    // BURN SEQUENCE

    mram_status_t mram_status = mram_init();
    for (uint8_t mram = 1; mram <= 3; mram++) {
        if (get_mram_state(mram_status, mram) != MRAM_STATE_OK) {
            warning_impl("[!] MRAM%u failed to initialize; the burn record relies on the other modules\n", mram);
        }
    }

    burn_record_t burn_record = load_burn_record();
    burn_record.boot_count++;
    
    // BEFORE-FLIGHT: remove
    burn_record.burned = true; 

    save_burn_record(&burn_record);
    // Called outside info(): in RELEASE builds info() compiles out, but this should always be printed
    info_impl("[+] Boot %lu; burn wire %s (%lu attempts), %lu s of the burn wait done\n", (unsigned long)burn_record.boot_count,
              burn_record.burned ? "burned" : "not burned", (unsigned long)burn_record.burn_attempts,
              (unsigned long)burn_record.elapsed_wait_s);

    if (!burn_record.burned) {
        // Wait out the rest of BURN_WAIT_MINUTES, saving progress every BURN_RECORD_SAVE_PERIOD_S so that a reboot
        // resumes the wait instead of restarting it. BURN_WAIT_MINUTES * 60 must be a multiple of the save period.
        while (burn_record.elapsed_wait_s < BURN_WAIT_MINUTES * 60u) {
            wait_seconds_feeding_watchdog(BURN_RECORD_SAVE_PERIOD_S);
            burn_record.elapsed_wait_s += BURN_RECORD_SAVE_PERIOD_S;
            save_burn_record(&burn_record);
            info("Burn wait: %lu/%lu s\n", (unsigned long)burn_record.elapsed_wait_s, (unsigned long)(BURN_WAIT_MINUTES * 60u));
        }

        // Recorded before burning, so that a reboot mid-burn shows up as an attempt
        burn_record.burn_attempts++;
        save_burn_record(&burn_record);

    // BEFORE-FLIGHT: define macro 
    #ifdef BURN_SEQUENCE_FIRE_BURN_WIRE
        // stuff commented out for utmost safety
        
        // BEFORE-FLIGHT: uncomment and test
        // gpio_set_pin_level(BURN, true); 
        
        info_impl("[+] Burn pin (PB29) on for %d s (attempt %lu)\n", BURN_ON_FOR_SECONDS, (unsigned long)burn_record.burn_attempts);
        wait_seconds_feeding_watchdog(BURN_ON_FOR_SECONDS);

        // BEFORE-FLIGHT: uncomment and test
        // gpio_set_pin_level(BURN, false);
        
        burn_record.burned = 1;
        save_burn_record(&burn_record);
        info_impl("[+] Burn pin (PB29) off; burn recorded in MRAM\n");
    #else
        info_impl("[+] BURN_SEQUENCE_FIRE_BURN_WIRE is not defined: burn wire not fired\n");
    #endif
    }

    // Initialize the mutex for the I2C bus shared by Arducam, fuel gauges, and PMB before startup checks/tests
    i2c_camera_bus_mutex = xSemaphoreCreateMutexStatic(&i2c_camera_bus_mutex_buffer);

    if (i2c_camera_bus_mutex == NULL) {
        fatal("Failed to create I2C_CAMERA bus mutex");
    }

    /* ---------- DEVICE CHECKS ---------- */

    configure_camera_rtt_channel();

    bool at_least_one_failed = check_all_devices_on_startup();
    info("AT_LEAST_ONE_DEVICE_FAILED: %d\n", at_least_one_failed);
    (void)at_least_one_failed; // unused when info() compiles out


    /* ---------- POST-BURN STEPS, TWO MINUTES APART ---------- */

#if BURN_SEQUENCE_UHF_MODE == BURN_UHF_MODE_LINK
    wait_minutes_before(POST_BURN_STEP_MINUTES, "the UHF tx/rx link");
    burn_uhf_link();
#elif BURN_SEQUENCE_UHF_MODE == BURN_UHF_MODE_BEACON
    wait_minutes_before(POST_BURN_STEP_MINUTES, "UHF beacons");
    burn_uhf_beacon();
#endif


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
