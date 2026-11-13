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
#include "../bootloader/src/image_format.h"
#include "globals.h" // defines BURN_SEQUENCE
#include "logging.h"
#include "mutexes.h"
#include "os_delay.h"
#include "arducam_driver.h"
#include "display_driver.h"
#include "drivers/at86rf215/at86_irq.h"
#include "burn/burn_uhf.h"
#include "image_buffer_PVDX.h"
#include "mram_map.h"
#include "ram_ecc.h"
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

// How often the burn wait saves its progress to MRAM, so a reboot resumes the wait instead of restarting it
#define BURN_RECORD_SAVE_PERIOD_S 60u


// Logs (and clears) the report stage 2 of the bootloader leaves in backup RAM about checking flash against MRAM
static void log_boot_report(void) {
    static const char *const golden_states[] = {"valid", "written from flash", "write failed", "unreadable", "disabled (NO_MRAM)"};
    volatile boot_report_t *report = (volatile boot_report_t *)BOOT_REPORT_ADDR;
    if (report->magic != BOOT_REPORT_MAGIC) {
        warning_impl("[!] No bootloader flash report\n");
        return;
    }
    const char *golden = report->golden_state < sizeof(golden_states) / sizeof(golden_states[0])
                             ? golden_states[report->golden_state]
                             : "unknown";
    info_impl("[+] Bootloader (stage 2 copy %u): golden copy %s, MRAM status 0x%02x\n", report->stage2_index, golden,
              report->mram_status);
    info_impl("[+] Bootloader blocks: %u checked, %u repaired, %u failed\n", report->blocks_checked, report->blocks_repaired,
              report->blocks_failed);
    if (report->blocks_failed != 0) {
        warning_impl("[!] Bootloader could not repair every flash block\n");
    }
    report->magic = 0; // So a stale report is never logged after a reset that skipped the bootloader
}

static status_t PVDX_init(void) {
    // Segger Buffer 0 is pre-configured at compile time according to segger documentation
    // Config the logging output channel (assuming it's not zero)
    if (LOGGING_RTT_OUTPUT_CHANNEL != 0) {
        SEGGER_RTT_ConfigUpBuffer(LOGGING_RTT_OUTPUT_CHANNEL, "Log Output", SEGGER_RTT_LOG_BUFFER, SEGGER_RTT_LOG_BUFFER_SIZE,
                                  SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    }
    return SUCCESS;
}

// Waits one second at a time.
// The watchdog task never runs either, so the hardware watchdog is normally off. It is fed every second anyway in
// case it is still running from an earlier boot.
[[maybe_unused]] static void wait_seconds_feeding_watchdog(uint32_t seconds) {
    for (uint32_t s = 0; s < seconds; s++) {
        watchdog_feed(WDT);
        os_delay_ms(1000);
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
        status_t status = init_arducam_hardware(ARDUCAM_DEFAULT_RESOLUTION);
        if (status == SUCCESS) {
            status = capture_rtt();
        }
        if (status == SUCCESS) {
            info("camera: picture sent over RTT channel %d\n", CAMERA_RTT_OUTPUT_CHANNEL);
            arducam_sleep();
            return;
        }
        warning("camera: attempt %lu/%d failed (status=%d)\n", (unsigned long)attempt, CAMERA_CAPTURE_ATTEMPTS, status);
    }
    warning("camera: giving up on the picture\n");
    arducam_sleep();
}

/* ---------- BURN WIRE STATE IN MRAM ---------- */

// Persists across reboots, as an MRAM record at the start of its own region (see mram_map.h)
typedef struct {
    uint32_t boot_count;     // boots seen since the record was created
    uint32_t burned;         // nonzero once the burn wire has been fired
    uint32_t burn_attempts;  // incremented just before each burn, so a reboot mid-burn is visible
    uint32_t elapsed_wait_s; // seconds of the pre-burn wait completed so far
} burn_record_t;

// Reads the record from the first MRAM module holding a valid copy. A missing record is replaced by a fresh one,
// which means the full wait runs again: waiting too long is safer than burning too early.
static burn_record_t load_burn_record(void) {
    burn_record_t record;
    if (!mram_record_load(MRAM_REGION_BURN_RECORD, 0, &record, sizeof(record))) {
        warning_impl("[!] No valid burn record in MRAM; starting a fresh one\n");
        record = (burn_record_t){0};
    }
    return record;
}

// Writes the record to all three MRAM modules
static void save_burn_record(const burn_record_t *const p_record) {
    if (!mram_record_save(MRAM_REGION_BURN_RECORD, 0, p_record, sizeof(*p_record))) {
        warning_impl("[!] Could not write the burn record to every MRAM chip\n");
    }
}

int main(void) {
    /* ---------- HARDWARE & LOGGING INITIALIZATION + BOOTLOADER CHECK ---------- */

    /* Initializes MCU, drivers and middleware */
    atmel_start_init();
    // Turn all LEDs off: they light when high, and Atmel Start drives every LED pin high
    gpio_set_pin_level(LED_ORANGE1, false);
    gpio_set_pin_level(LED_ORANGE2, false);
    gpio_set_pin_level(LED_RED, false);
    PVDX_init();
    // info_impl(RTT_CTRL_RESET RTT_CTRL_CLEAR); // Reset the terminal
    info("--- Atmel & Hardware Initialization Complete ---\n");
    info("[+] Build Type: %s\n", BUILD_TYPE);
    info("[+] Build Date: %s\n", BUILD_DATE);
    info("[+] Build Time: %s\n", BUILD_TIME);
    info("[+] Built from branch: %s\n", GIT_BRANCH_NAME);
    info("[+] Built from commit: %s\n", GIT_COMMIT_HASH);

    ram_ecc_init();

    // BURN SEQUENCE

    // The only mram_map_init() call; everything else uses mram_map_status()
    mram_status_t mram_status = mram_map_init();
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
    i2c_sensors_bus_mutex = xSemaphoreCreateMutexStatic(&i2c_sensors_bus_mutex_buffer);

    if (i2c_sensors_bus_mutex == NULL) {
        fatal("Failed to create I2C_SENSORS bus mutex");
    }
    // atmel_start_init() initializes the shared buses but leaves them disabled; the drivers on them assume they are
    // enabled
    if (i2c_m_sync_enable(&I2C_SENSORS) != 0) {
        warning("Failed to enable the I2C_SENSORS bus\n");
    }
    spi_m_sync_enable(&SPI_MAGNETOMETER_GYRO);

    // The UHF radio's IRQ line stays disarmed until a task waits on it, so this is safe before the radio is set up
    at86_irq_init();

    log_boot_report();

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

    init_adc1_mutex();

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
