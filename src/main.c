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
#include "mutexes.h"

#ifdef BURN_SEQUENCE
    #include <string.h>

    #include "arducam_driver.h"
    #include "display_driver.h"
    #include "drivers/at86rf215/at86rf215.h"
    #include "drivers/at86rf215/regs.h"
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

    // UHF beacon: same legacy O-QPSK setup as test_at86_tx() on the at86-tx-rx-test branch
    #define UHF_TX_FREQ_HZ       437765000UL
    #define UHF_TX_POWER         10
    #define UHF_BEACON_COUNT     10
    #define UHF_BEACON_PERIOD_MS 1000
    #define UHF_POLL_LIMIT       10000 // polls 10 us apart, so ~100 ms before giving up on a radio state

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

/* ---------- STEP 1: UHF BEACON ---------- */

static struct at86rf215 uhf_dev;
static const char uhf_beacon_text[] = "PVDX post-burn UHF beacon";

// Bails out of the calling bool function if an AT86RF215 call fails
    #define UHF_TRY(call)                                         \
        do {                                                      \
            int uhf_ret = (call);                                 \
            if (uhf_ret != AT86RF215_OK) {                        \
                warning("uhf: %s failed (%d)\n", #call, uhf_ret); \
                return false;                                     \
            }                                                     \
        } while (0)

static bool uhf_wait_state(at86rf215_rf_state_t want) {
    at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;
    for (uint32_t i = 0; i < UHF_POLL_LIMIT; i++) {
        if (at86rf215_get_state(&uhf_dev, &state, AT86RF215_RF09) == AT86RF215_OK && state == want) {
            return true;
        }
        at86rf215_delay_us(&uhf_dev, 10);
    }
    return false;
}

static bool uhf_setup(void) {
    spi_m_sync_enable(&SPI_UHF);

    uhf_dev.clko_os = AT86RF215_RF_CLKO_OFF;
    uhf_dev.clk_drv = AT86RF215_RF_DRVCLKO2;
    uhf_dev.rf_femode_09 = AT86RF215_RF_FEMODE1;
    uhf_dev.rf_femode_24 = AT86RF215_RF_FEMODE0;
    uhf_dev.xo_fs = 0;
    uhf_dev.xo_trim = 0;
    uhf_dev.irqmm = 0;
    uhf_dev.irqp = 0;
    uhf_dev.pad_drv = AT86RF215_RF_DRV2;

    UHF_TRY(at86rf215_init(&uhf_dev));
    UHF_TRY(at86rf215_conn_check(&uhf_dev));
    UHF_TRY(at86rf215_set_mode(&uhf_dev, AT86RF215_RF_MODE_BBRF));

    const struct at86rf215_radio_conf radio_conf = {
        .cm = AT86RF215_CM_FINE_RES_04,
        .cs = 0,
        .base_freq = 0,
        .lbw = AT86RF215_PLL_LBW_DEFAULT,
    };
    UHF_TRY(at86rf215_radio_conf(&uhf_dev, AT86RF215_RF09, &radio_conf));

    struct at86rf215_bb_conf bb;
    memset(&bb, 0, sizeof(bb));
    bb.pt = AT86RF215_BB_MROQPSK;
    bb.fcst = AT86RF215_FCS_16;
    bb.txafcs = 1;
    bb.qpsk.fchip = AT86RF215_MROQPSK_FCHIP_2000;
    bb.qpsk.fshape_mod = AT86RF215_MROQPSK_BB_RRC08;
    bb.qpsk.drate_mod = AT86RF215_MROQPSK_DRATE_MOD_0;
    bb.qpsk.leg = 1;     // OQPSKPHRTX.LEG: transmit legacy O-QPSK
    bb.qpsk.fcstleg = 1; // OQPSKC2.FCSTLEG: 16 bit FCS for legacy
    bb.qpsk.rxm = AT86RF215_MROQPSK_RXM_LEGACY_ONLY;
    bb.qpsk.nsfd = AT86RF215_MROQPSK_NSFD_SFD1;
    UHF_TRY(at86rf215_bb_conf(&uhf_dev, AT86RF215_RF09, &bb));
    UHF_TRY(at86rf215_bb_enable(&uhf_dev, AT86RF215_RF09, 1));

    UHF_TRY(at86rf215_set_freq(&uhf_dev, AT86RF215_RF09, UHF_TX_FREQ_HZ));
    UHF_TRY(at86rf215_set_pac(&uhf_dev, AT86RF215_RF09, AT86RF215_PACUR_NO_RED, UHF_TX_POWER));

    UHF_TRY(at86rf215_set_cmd(&uhf_dev, AT86RF215_CMD_RF_TXPREP, AT86RF215_RF09));
    if (!uhf_wait_state(AT86RF215_STATE_RF_TXPREP)) {
        warning("uhf: radio did not reach TXPREP\n");
        return false;
    }

    at86rf215_pll_ls_t pll = AT86RF215_PLL_UNLOCKED;
    UHF_TRY(at86rf215_get_pll_ls(&uhf_dev, &pll, AT86RF215_RF09));
    if (pll != AT86RF215_PLL_LOCKED) {
        warning("uhf: PLL did not lock\n");
        return false;
    }
    return true;
}

// at86rf215_tx_frame() waits for a TX-complete interrupt that nothing wires up here, so poll BBC0_IRQS.TXFE instead
static bool uhf_send_frame(const uint8_t *psdu, size_t len) {
    // The chip appends the 2 byte FCS (BBC0_PC.TXAFCS), so the frame length must cover it
    const size_t frame_len = len + 2;
    UHF_TRY(at86rf215_reg_write_8(&uhf_dev, frame_len & 0xFF, REG_BBC0_TXFLL));
    UHF_TRY(at86rf215_reg_write_8(&uhf_dev, (frame_len >> 8) & 0xFF, REG_BBC0_TXFLH));

    // Address (with the write bit) and data must go out in a single at86rf215_spi_write(), which drives chip select
    // itself. The buffer holds the 2 FCS octets too, which the chip overwrites
    uint8_t burst[2 + sizeof(uhf_beacon_text) + 2];
    if (len > sizeof(uhf_beacon_text)) {
        return false;
    }
    burst[0] = (REG_BBC0_FBTXS >> 8) | 0x80;
    burst[1] = REG_BBC0_FBTXS & 0xFF;
    memcpy(&burst[2], psdu, len);
    memset(&burst[2 + len], 0, 2);
    UHF_TRY(at86rf215_spi_write(&uhf_dev, burst, 2 + frame_len));

    uint8_t irqs = 0;
    UHF_TRY(at86rf215_reg_read_8(&uhf_dev, &irqs, REG_BBC0_IRQS)); // reading clears any stale TXFE
    UHF_TRY(at86rf215_set_cmd(&uhf_dev, AT86RF215_CMD_RF_TX, AT86RF215_RF09));

    for (uint32_t i = 0; i < UHF_POLL_LIMIT; i++) {
        UHF_TRY(at86rf215_reg_read_8(&uhf_dev, &irqs, REG_BBC0_IRQS));
        if (irqs & (1 << 4)) { // TXFE: frame fully transmitted
            return true;
        }
        at86rf215_delay_us(&uhf_dev, 10);
    }
    warning("uhf: frame did not finish transmitting\n");
    return false;
}

static void send_uhf_beacons(void) {
    info("uhf: setting up radio (%lu Hz, power %d)\n", (unsigned long)UHF_TX_FREQ_HZ, UHF_TX_POWER);
    if (!uhf_setup()) {
        warning("uhf: setup failed, skipping UHF beacons\n");
        return;
    }

    // Each frame is a sequence number followed by the beacon text (without its NUL)
    uint8_t psdu[sizeof(uhf_beacon_text)];
    memcpy(&psdu[1], uhf_beacon_text, sizeof(uhf_beacon_text) - 1);
    uint32_t sent = 0;
    for (uint32_t n = 0; n < UHF_BEACON_COUNT; n++) {
        psdu[0] = (uint8_t)n;
        if (uhf_send_frame(psdu, sizeof(psdu))) {
            sent++;
        }
        // Return to TXPREP for the next frame
        at86rf215_set_cmd(&uhf_dev, AT86RF215_CMD_RF_TXPREP, AT86RF215_RF09);
        if (!uhf_wait_state(AT86RF215_STATE_RF_TXPREP)) {
            warning("uhf: radio did not return to TXPREP\n");
        }
        watchdog_feed(WDT);
        delay_ms(UHF_BEACON_PERIOD_MS);
    }

    at86rf215_set_cmd(&uhf_dev, AT86RF215_CMD_RF_TRXOFF, AT86RF215_RF09);
    info("uhf: sent %lu/%d beacons, radio off\n", (unsigned long)sent, UHF_BEACON_COUNT);
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

static void send_picture_over_rtt(void) {
    #if defined(RELEASE)
    // No host drains channel 2 in flight, so drop image bytes that don't fit rather than hang
    const unsigned rtt_image_mode = SEGGER_RTT_MODE_NO_BLOCK_SKIP;
    #else
    // Block so image bytes are not dropped. NOTE: this hangs until a host reads RTT channel 2
    const unsigned rtt_image_mode = SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL;
    #endif
    SEGGER_RTT_ConfigUpBuffer(CAMERA_RTT_OUTPUT_CHANNEL, "Image", camera_rtt_image_buffer, SEGGER_RTT_IMAGE_BUFFER_SIZE,
                              rtt_image_mode);

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

    // Initialize the mutex for the I2C bus shared by Arducam, fuel gauges, and PMB before startup checks/tests
    i2c_camera_bus_mutex = xSemaphoreCreateMutexStatic(&i2c_camera_bus_mutex_buffer);

    if (i2c_camera_bus_mutex == NULL) {
        fatal("Failed to create I2C_CAMERA bus mutex");
    }

    /* ---------- DEVICE CHECKS ---------- */

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

    wait_minutes_before(POST_BURN_STEP_MINUTES, "UHF beacons");
    send_uhf_beacons();

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
