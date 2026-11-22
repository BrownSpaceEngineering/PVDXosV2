/**
 * uhf_radio_main.c
 *
 *
 * Created: October 7, 2026
 * Modified: November 20, 2026
 * Authors: Noah Shepard
 *
 * TODO (UHF task):
 * - Verify every DATASHEET(verify) tag and the UHF_TX_TIMEOUT_MS airtime estimate on hardware
 * - Add the datasheet Errata #2 workaround (RFn_PLL 9 -> 8) where marked ERRATA(#2): uhf_chip_setup()'s TRXOFF -> TXPREP.
 *   Only v.1 silicon (RF_VN = 1) needs it
 * - Tune the PUSP timing constants in uhf_radio_task.h against the real ground station
 * - Confirm CCSDS_APID_TELEMETRY (0x04) and the one-byte PUSP payload format, both temporary
 * - Confirm the placeholder RF settings (UHF_FREQ_HZ, UHF_TX_POWER, UHF_PHY_CONFIG) with the ground station
 * - Hardware: pick RF09's front-end mode (PA/LNA switch wiring) and trim xo_fs/xo_trim, see at86_init_pvdx()
 * - Enable the task in task_list.c and settle its priority
 *
 * TODO (SPP / CFDP):
 * - spp_wrap() (radio_routing.c) sends a placeholder callsign and zero timestamps on every downlink, beacons and PUSP
 *   directives included. Needs the real callsign before transmitting on UHF
 * - More in TODO.md at the repository root
 */
#include <sys/types.h>

#include "at86rf215.h"
#include "ccsds/radio_routing.h"
#include "ccsds/uslp.h"
#include "drivers/rtc/rtc_driver.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "portmacro.h"
#include "projdefs.h"
#include "rtos_start.h"
#include "string.h"
#include "uhf_radio_task.h"
#include "watchdog_task.h"

// Radio Task memory structures
uhf_radio_task_memory_t uhf_radio_mem;
QueueHandle_t downlink_queue;
QueueHandle_t uplink_queue;

// Uplink and downlink assume one space packet per USLP frame and one USLP frame per radio frame
_Static_assert(RADIO_DOWNLINK_MAX_SIZE <=
                   USLP_MAX_FRAME_SIZE - (USLP_PRIMARY_HEADER_FIXED_SIZE + 1 + USLP_DATA_FIELD_HEADER_SIZE + USLP_FECF_SIZE),
               "a space packet must fit in one USLP frame");
_Static_assert(USLP_MAX_FRAME_SIZE <= AT86RF215_MAX_PDU, "a USLP frame must fit in one radio frame");

/**
 * \fn init_uhf_radio
 *
 * \brief Initialises radio command queue, before `init_task_pointer()`.
 *
 * \returns QueueHandle_t, a handle to the created queue
 *
 * \see `init_task_pointer()` for usage of functions of the type `init_<TASK>()`
 */
QueueHandle_t init_uhf_radio(void) {
    downlink_queue = xQueueCreateStatic(RADIO_DOWNLINK_QUEUE_LENGTH, sizeof(radio_queue_item_t), uhf_radio_mem.downlink_queue_buffer,
                                        &uhf_radio_mem.downlink_queue_mem);

    uplink_queue = xQueueCreateStatic(RADIO_UPLINK_QUEUE_LENGTH, sizeof(radio_queue_item_t), uhf_radio_mem.uplink_queue_buffer,
                                      &uhf_radio_mem.uplink_queue_mem);
    if (downlink_queue == NULL) {
        fatal("Failed to create radio downlink queue!\n");
    }

    if (uplink_queue == NULL) {
        fatal("Failed to create radio uplink queue!\n");
    }

    return NULL;
}

void uhf_watchdog_checkin(void) {
    // Cached, to avoid building the command on every check-in
    static command_t cmd_checkin;
    static bool have_cmd = false;
    static bool checked_in = false;
    static TickType_t last_checkin = 0;

    if (!should_checkin(p_uhf_radio_task)) {
        return;
    }
    const TickType_t now = xTaskGetTickCount();
    if (checked_in && now - last_checkin < pdMS_TO_TICKS(UHF_CHECKIN_PERIOD_MS)) {
        return;
    }

    if (!have_cmd) {
        cmd_checkin = get_watchdog_checkin_command(p_uhf_radio_task);
        have_cmd = true;
    }
    enqueue_command(&cmd_checkin);
    checked_in = true;
    last_checkin = now;
    debug("Radio: Enqueued watchdog checkin command\n");
}

/**
 * \fn main_uhf_radio
 *
 * \param pvParameters a void pointer to the parameters required by radio functions; not currently set by config
 *
 * \warning should never return
 */
void main_uhf_radio(void *pvParameters) {
    info("radio: Task Started!\n");

    // Keep checking in while retrying, so that a dead radio doesn't get the whole satellite reset by the watchdog
    while (!uhf_chip_setup()) {
        warning("Radio: chip setup failed, retrying in %d ms\n", UHF_SETUP_RETRY_MS);
        uhf_watchdog_checkin();
        vTaskDelay(pdMS_TO_TICKS(UHF_SETUP_RETRY_MS));
    }

    at86_irq_setup();

    const TickType_t start = xTaskGetTickCount();
    uhf_ctx_t ctx = {.state = LISTEN, .last_rx = start, .next_beacon = start + pdMS_TO_TICKS(UHF_BEACON_PERIOD_MS)};

    // The RTC is calibrated against the radio's timestamp counter here, since this task owns the radio's SPI bus
    TickType_t next_rtc_calibration = start + pdMS_TO_TICKS(RTC_CALIBRATE_PERIOD_MS);

    while (true) {
        TickType_t wait = uhf_ticks_until_wake(&ctx);
        const TickType_t until_rtc_calibration = uhf_ticks_until(xTaskGetTickCount(), next_rtc_calibration);
        wait = until_rtc_calibration < wait ? until_rtc_calibration : wait;

        uint32_t events = 0;
        xTaskNotifyWait(0, UINT32_MAX, &events, wait);

        const uhf_rx_t rx = uhf_rx_poll(&ctx, events);
        uhf_step(&ctx, &rx);

        if (uhf_deadline_passed(next_rtc_calibration)) {
            calibrate_rtc();
            next_rtc_calibration = xTaskGetTickCount() + pdMS_TO_TICKS(RTC_CALIBRATE_PERIOD_MS);
        }

        uhf_watchdog_checkin();
    }
}
