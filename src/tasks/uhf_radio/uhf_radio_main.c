/**
 * uhf_radio_main.c
 *
 *
 * Created: October 7, 2026
 * Modified: October 7, 2026
 * Authors: Noah Shepard
 *
 * TODO (UHF task):
 * - Replace the dummy uhf_rx_poll() (drops every frame) with a refactor of uhf_rx_process(), then delete
 *   uhf_rx_process() and uhf_pusp_rx(). Decide whether to drop packets whose APID doesn't match their VC
 * - Replace the dummy uhf_transmit_pusp() (sends nothing): APID CCSDS_APID_PUSP on RADIO_STREAM_PUSP's VC, wait for
 *   TXFE, back to RX. Give the PUSP tests a seam so they don't transmit (they run before the radio is set up)
 * - Make uhf_tx_process() block until TXFE, like uhf_transmit_beacon()
 * - Verify every DATASHEET(verify) tag and the UHF_TX_TIMEOUT_MS airtime estimate on hardware
 * - Add the datasheet Errata #2 workaround (RFn_PLL 9 -> 8) to uhf_chip_setup()'s TRXOFF -> TXPREP
 * - Mask TRXRDY from the IRQ pin (state changes are polled, so it only causes spurious wakeups)
 * - Tune the PUSP timing constants in uhf_radio_task.h against the real ground station
 * - Confirm CCSDS_APID_TELEMETRY (0x04) and the one-byte PUSP payload format, both temporary
 * - Enable the task in task_list.c and settle its priority
 * - Update or delete radio_routing_plan.md, which still describes the removed telemetry timer
 *
 * TODO (CFDP task changes):
 * - radio_route_send() should notify this task with UHF_EVENT_DOWNLINK after queueing an item
 * - Yield the turn when CFDP marks an item as its terminal PDU, instead of on an empty queue after UHF_YIELD_GRACE_MS
 *   (replaces the grace logic in establish_send_proc()). BLOCKED: CFDP needs to add the flag to radio_queue_item_t
 * - Drop to one active CFDP transaction (MAX_TRANSACTIONS)
 * - The downlink queue only drains in ESTABLISH_SEND: decide what CFDP does when it fills with no link up
 */
#include <sys/types.h>

#include "at86rf215.h"
#include "ccsds/radio_routing.h"
#include "ccsds/uslp.h"
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

    while (true) {
        uint32_t events = 0;
        xTaskNotifyWait(0, UINT32_MAX, &events, uhf_ticks_until_wake(&ctx));

        const uhf_rx_t rx = uhf_rx_poll(&ctx, events);
        const uhf_state_t next = uhf_state_descs[ctx.state].proc(&ctx, &rx);
        if (next != ctx.state) {
            uhf_enter_state(&ctx, next);
        }

        uhf_watchdog_checkin();
    }
}
