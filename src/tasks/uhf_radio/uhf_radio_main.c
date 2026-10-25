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

    // local uplink/downlink_queue item
    radio_queue_item_t item;

    bool needs_downlink = false;

    while (true) {
        xTaskNotifyWait(0, UINT32_MAX, &events, portMAX_DELAY);

        if (events & UHF_EVENT_RXFE) {
            at86_pop_frame(at86_buffer, sizeof(at86_buffer));
            if (needs_downlink) {
                uhf_downlink_frames();
                needs_downlink = false;
            } else {
                uhf_go_rx();
            }
        }

        if (events & UHF_EVENT_TXFE || events & UHF_EVENT_TXRDY) {
            if (needs_downlink) {
                uhf_downlink_frames();
                needs_downlink = false;
            } else {
                uhf_go_rx();
            }
        }

        if (events & UHF_EVENT_DOWNLINK) {
            needs_downlink = true;
            uhf_go_txprep();
        }

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("Radio: Enqueued watchdog checkin command\n");
        }

        if (events & UHF_EVENT_DOWNLINK) {
            int ret = set_at86_tx_prep_mode(&at86_dev, AT86_STATE_CHANGE_TIMEOUT_MS);
            if (ret) {
                warning("Radio: failed to enter TXPREP (%d), dropping packet\n", ret);
                continue;
            }

            while (xQueueReceive(downlink_queue, &item, 0) == pdPASS) {
                size_t frame_sz = AT86RF215_MAX_PDU;
                if (uslp_mapp_request(at86_buffer, &frame_sz, item.data, item.len, RADIO_GMAP_ID(item.vcid), SPP_VERSION_NUMBER,
                                      RADIO_SDU_ID, USLP_QOS_EXPEDITED)) {
                    warning("USLP: failed to prepare frame\n");
                    continue;
                }

                ret = at86_send_frame(&at86_dev, at86_buffer, frame_sz, AT86_TX_TIMEOUT_MS);
                if (ret) {
                    warning("Radio: failed to send frame (%d)\n", ret);
                    continue;
                }
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

            if (!radio_route_accepts_uplink(RADIO_UHF, tframe_view.primary_header.virtual_channel_id)) {
                warning("Radio: dropping USLP frame on unexpected VC %u\n", tframe_view.primary_header.virtual_channel_id);
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
            item.vcid = tframe_view.primary_header.virtual_channel_id;
            memcpy(item.data, tframe_view.datafield, tframe_view.datafield_len);

            if (xQueueSend(uplink_queue, &item, 0) != pdPASS) {
                warning("Radio: uplink queue full, dropping packet\n");
            }
        }
    }
}
