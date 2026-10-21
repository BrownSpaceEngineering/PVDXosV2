/**
 * uhf_radio_main.c
 *
 *
 * Created: October 7, 2026
 * Modified: October 7, 2026
 * Authors: Noah Shepard
 */
#include "at86rf215.h"
#include "ccsds/uslp.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
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

uint8_t at86_buffer[AT86RF215_MAX_PDU];

// TODO(chip setup): this handle is never initialised. Before the task touches the radio it needs, in order:
//   1. spi_m_sync_enable(&SPI_UHF) (currently only done inside at86_check_health)
//   2. at86_init_pvdx(&at86_dev)
//   3. at86rf215_radio_conf(&at86_dev, AT86RF215_RF09, ...) for carrier frequency/channel, set while in TRXOFF
//   4. TX power (at86rf215_set_pac), also in TRXOFF so the PA calibrates against it
//   5. change_at86_config(...) to pick the PHY
//   6. set_at86_rx_mode(&at86_dev, ...) so the radio starts out listening
static struct at86rf215 at86_dev;

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
    QueueHandle_t uhf_radio_command_queue_handle =
        xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, uhf_radio_mem.uhf_radio_command_queue_buffer,
                           &uhf_radio_mem.uhf_radio_task_queue);

    if (uhf_radio_command_queue_handle == NULL) {
        fatal("Failed to create radio command queue!\n");
    }

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

    return uhf_radio_command_queue_handle;
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
    // Calculate the maximum time this task should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Variable to hold commands popped off the queue
    command_t cmd;
    radio_queue_item_t item;

    while (true) {
        // Block waiting for at least one command to appear in the command queue
        if (xQueueReceive(p_uhf_radio_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            // Once there is at least one command in the queue, empty the entire queue
            info("Radio: performing command\n");
        }
        debug("Radio: No more commands queued.\n");

        if (xQueueReceive(downlink_queue, &item, queue_block_time_ticks) == pdPASS) {
            int ret = set_at86_tx_prep_mode(&at86_dev, AT86_STATE_CHANGE_TIMEOUT_MS);
            if (ret) {
                warning("Radio: failed to enter TXPREP (%d), dropping packet\n", ret);
            } else {
                // Send every queued packet, one USLP frame per packet
                do {
                    size_t frame_sz = AT86RF215_MAX_PDU;
                    if (uslp_mapp_request(at86_buffer, &frame_sz, item.data, item.len, RADIO_GMAP_ID, SPP_VERSION_NUMBER, RADIO_SDU_ID,
                                          USLP_QOS_EXPEDITED)) {
                        warning("USLP: failed to prepare frame\n");
                        continue;
                    }

                    ret = at86_send_frame(&at86_dev, at86_buffer, frame_sz, AT86_TX_TIMEOUT_MS);
                    if (ret) {
                        warning("Radio: failed to send frame (%d)\n", ret);
                    }

                    // A full frame at the lowest data rate takes seconds, so a long burst could outlast the watchdog
                    if (should_checkin(current_task)) {
                        enqueue_command(&cmd_checkin);
                    }
                } while (xQueueReceive(downlink_queue, &item, 0) == pdPASS);
            }

            ret = set_at86_rx_mode(&at86_dev, AT86_STATE_CHANGE_TIMEOUT_MS);
            if (ret) {
                warning("Radio: failed to return to RX (%d)\n", ret);
            }
        }

        // Each radio frame carries exactly one USLP frame, which carries exactly one space packet
        while (at86_peek_frame()) {
            int sz = at86_pop_frame(at86_buffer, sizeof(at86_buffer));
            if (sz < 0) {
                warning("Radio: failed to pop frame from radio\n");
                continue;
            }

            uslp_transfer_frame_view_t tframe_view;
            if (uslp_transfer_frame_parse(&tframe_view, at86_buffer, (uint32_t)sz)) {
                warning("Radio: dropping malformed USLP frame\n");
                continue;
            }

            if (tframe_view.data_field_header.tfdz_construction_rules != USLP_TFDZ_NO_SEGMENT ||
                tframe_view.data_field_header.protocol_identifier != USLP_UPID_SPACE_PACKETS) {
                warning("Radio: dropping USLP frame that doesn't carry one whole space packet\n");
                continue;
            }

            if (tframe_view.datafield_len == 0 || tframe_view.datafield_len > sizeof(item.data)) {
                warning("Radio: dropping USLP frame with a %u byte data field\n", tframe_view.datafield_len);
                continue;
            }

            item.len = tframe_view.datafield_len;
            memcpy(item.data, tframe_view.datafield, tframe_view.datafield_len);

            // Don't block here: more frames may be waiting and the watchdog still needs a checkin
            if (xQueueSend(uplink_queue, &item, 0) != pdPASS) {
                warning("Radio: uplink queue full, dropping packet\n");
            }
        }

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("Radio: Enqueued watchdog checkin command\n");
        }
    }
}
