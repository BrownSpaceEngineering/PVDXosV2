/**
 * uhf_radio_main.c
 *
 *
 * Created: October 7, 2026
 * Modified: October 7, 2026
 * Authors: Noah Shepard
 */
#include "at86rf215.h"
#include "ccsds/radio_routing.h"
#include "ccsds/uslp.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "portmacro.h"
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

/**
 * \fn main_uhf_radio
 *
 * \param pvParameters a void pointer to the parameters required by radio functions; not currently set by config
 *
 * \warning should never return
 */
void main_uhf_radio(void *pvParameters) {
    info("radio: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);

    // event flags
    uint32_t events;

    bool needs_downlink = false;

    while (true) {
        xTaskNotifyWait(0, UINT32_MAX, &events, portMAX_DELAY);

        if (events & UHF_EVENT_DOWNLINK) {
            needs_downlink = true;
            uhf_go_txprep();
        }

        if (events & UHF_EVENT_RXFE) {
            uhf_rx_process();
            if (needs_downlink) {
                needs_downlink = uhf_tx_process();
            } else {
                uhf_go_rx();
            }
        }

        if (events & UHF_EVENT_TXFE || events & UHF_EVENT_TXRDY) {
            if (needs_downlink) {
                needs_downlink = uhf_tx_process();
            } else {
                uhf_go_rx();
            }
        }

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("Radio: Enqueued watchdog checkin command\n");
        }
    }
}
