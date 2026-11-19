/**
 * src/tasks/cfdp/radio_routing.c
 *
 * Routes downlinked packets to the radio task for their stream
 *
 * Created: 20261023 FRI
 * Updated: 20261023 FRI
 * Authors: Noah Shepard
 */

#include "ccsds/radio_routing.h"

#include "ccsds/uslp.h"
#include "drivers/stxg2/stxg2.h"
#include "logging.h"
#include "queue.h"
#include "tasks/task_list.h"

// Reference: radio_routing_info.md
const radio_route_t radio_routes[RADIO_STREAM_COUNT] = {
    [RADIO_STREAM_PEROVSKITE] = {.vcid = 0, .radio = RADIO_SBAND}, // on request by uplink
    [RADIO_STREAM_IMAGE] = {.vcid = 1, .radio = RADIO_SBAND},      // on request by uplink
    [RADIO_STREAM_TELEMETRY] = {.vcid = 2, .radio = RADIO_UHF},    // UHF beacon bursts, without CFDP
    [RADIO_STREAM_CFDP_REPLY] = {.vcid = 3, .radio = RADIO_UHF},   // whenever CFDP sends an ACK/NAK/FIN
    [RADIO_STREAM_UPLINK_CMD] = {.vcid = 4, .radio = RADIO_UHF},   // when uplinked
    [RADIO_STREAM_PUSP] = {.vcid = 5, .radio = RADIO_UHF},         // UHF link handshakes, both ways
};

// S-band framing buffers. Static, since a whole frame would take half of CFDP's stack. Only CFDP sends on S-band
static uint8_t sband_spp_buf[RADIO_DOWNLINK_MAX_SIZE];
static uint8_t sband_frame_buf[USLP_MAX_FRAME_SIZE];

static bool sband_open = false;       // The STXG2 is powered on with a store session open
static uint32_t sband_next_index = 0; // First free memory packet in the STXG2's flash. Starts at 0 on every boot
static uint32_t sband_file_start = 0; // Memory packet the open session started at

/**
 * Powers the STXG2 on and starts a store session at the next free memory packet. If that's past the end of the radio's
 * memory, wraps to 0 and overwrites the oldest files
 */
static status_t sband_open_session(void) {
    status_t result = stxg2_init();
    if (result != SUCCESS) {
        return result;
    }
    result = stxg2_store_begin(sband_next_index);
    // ERROR_BAD_ARGS: the index is at or past MEMORY_SIZE (stxg2_check_index())
    if (result == ERROR_BAD_ARGS && sband_next_index != 0) {
        info("radio routing: S-band flash full at packet %lu, wrapping to 0\n", (unsigned long)sband_next_index);
        sband_next_index = 0;
        result = stxg2_store_begin(sband_next_index);
    }
    if (result == SUCCESS) {
        sband_file_start = sband_next_index;
    }
    return result;
}

/**
 * The next SPP sequence count for apid. CCSDS counts packets separately for each APID (SPP Blue Book 4.1.3.4.3)
 */
static uint16_t spp_next_seq_count(uint16_t apid) {
    static uint16_t cfdp_count = 0;
    static uint16_t pusp_count = 0;
    static uint16_t other_count = 0;

    uint16_t *count;
    switch (apid) {
        case CCSDS_APID_CFDP:
            count = &cfdp_count;
            break;
        case CCSDS_APID_PUSP:
            count = &pusp_count;
            break;
        default:
            count = &other_count;
            break;
    }
    uint16_t ret = *count & 0x3FFF;
    *count = (uint16_t)((*count + 1) & 0x3FFF);
    return ret;
}

bool spp_wrap(const radio_queue_item_t *it, uint8_t *buf, size_t buf_size, size_t *packet_len) {
    spp_primary_packet_header_t primary_header = {
        .version_number = SPP_VERSION_NUMBER,
        .packet_type = SPP_PACKET_TYPE_REPORTING,
        .secondary_header_flag = SPP_SECONDARY_HEADER_PRESENT,
        .application_process_id = it->apid,
        .sequence_flags = SPP_SEQ_FLAG_UNSEGMENTED_DATA,
        .sequence_count = spp_next_seq_count(it->apid),
    };

    // TODO(temp): placeholder timestamps and callsign. Transmitting on UHF needs the real callsign before flight
    spp_secondary_packet_header_t secondary_header = {
        .timestamp = 0,                             // TODO(temp): unix time
        .boot_timestamp = 0,                        // TODO(temp): unix time of last boot
        .callsign = {'B', 'S', 'E', 'B', 'S', 'E'}, // TODO(temp): real callsign
    };

    return spp_packet_serialize(&primary_header, &secondary_header, it->data, it->len, buf, buf_size, packet_len);
}

// Streams the ground sends to us. Ground replies (ACK/NAK/FIN) to our downlinked files come in on the CFDP reply VC, and
// the ground's PUSP directives on the PUSP VC
static const radio_stream_t uplink_streams[] = {RADIO_STREAM_CFDP_REPLY, RADIO_STREAM_UPLINK_CMD, RADIO_STREAM_PUSP};

int radio_route_send(radio_stream_t stream, radio_queue_item_t *item, TickType_t wait) {
    if (stream >= RADIO_STREAM_COUNT || item == NULL || item->len == 0 || item->len > sizeof(item->data)) {
        return -RADIO_ERR_INVAL;
    }

    const radio_route_t *route = &radio_routes[stream];
    item->vcid = route->vcid;

    switch (route->radio) {
        case RADIO_UHF:
            if (xQueueSend(downlink_queue, item, wait) != pdPASS) {
                return -RADIO_ERR_QUEUE_FULL;
            }
            if (p_uhf_radio_task->handle != NULL) {
                xTaskNotify(p_uhf_radio_task->handle, UHF_EVENT_DOWNLINK, eSetBits);
            }
            return RADIO_OK;
        case RADIO_SBAND:
            {
                // No S-band task: the packet is framed here and stored straight into the STXG2's flash, to be downlinked
                // later. The first packet of a file powers the radio on and opens a store session; CFDP closes it with
                // radio_sband_close() once the file is done
                size_t packet_len = 0;
                if (spp_wrap(item, sband_spp_buf, sizeof(sband_spp_buf), &packet_len)) {
                    warning("radio routing: failed to wrap %u byte S-band packet for APID 0x%x\n", item->len, item->apid);
                    return -RADIO_ERR_INVAL;
                }

                size_t frame_sz = sizeof(sband_frame_buf);
                if (uslp_mapp_request(sband_frame_buf, &frame_sz, sband_spp_buf, (uint16_t)packet_len, RADIO_GMAP_ID(item->vcid),
                                      SPP_VERSION_NUMBER, RADIO_SDU_ID, USLP_QOS_EXPEDITED)) {
                    warning("radio routing: failed to build S-band USLP frame\n");
                    return -RADIO_ERR_INVAL;
                }

                // Framed first, so a bad packet never powers the radio on
                if (!sband_open) {
                    if (sband_open_session() != SUCCESS) {
                        warning("radio routing: failed to open S-band store session\n");
                        stxg2_power_off();
                        return -RADIO_ERR_SBAND_STORE;
                    }
                    sband_open = true;
                }

                if (stxg2_store(sband_frame_buf, frame_sz) != SUCCESS) {
                    return -RADIO_ERR_SBAND_STORE;
                }
                return RADIO_OK;
            }
        default:
            return -RADIO_ERR_INVAL;
    }
}

void radio_sband_close(void) {
    if (!sband_open) {
        return;
    }
    uint32_t packets_written = 0;
    if (stxg2_store_end(&packets_written) != SUCCESS) {
        // Not advanced: whatever was written is garbage, and the next file overwrites it
        warning("radio routing: S-band store end failed\n");
    } else {
        sband_next_index = sband_file_start + packets_written;
        debug("radio routing: S-band file stored at packets %lu-%lu\n", (unsigned long)sband_file_start,
              (unsigned long)(sband_next_index - 1));
    }
    stxg2_power_off();
    sband_open = false;
}

bool radio_route_accepts_uplink(radio_id_t radio, uint8_t vcid) {
    for (size_t i = 0; i < sizeof(uplink_streams) / sizeof(uplink_streams[0]); i++) {
        const radio_route_t *route = &radio_routes[uplink_streams[i]];
        if (route->radio == radio && route->vcid == vcid) {
            return true;
        }
    }
    return false;
}

// Link state. Each field is written by one task only, and all are read and written under link_lock() so they're
// consistent with each other. The _FROM_ISR critical sections save and restore the interrupt mask instead of counting
// nesting, so they're also safe before the scheduler starts (the PUSP tests change UHF states then)
#define link_lock() UBaseType_t link_lock_mask = taskENTER_CRITICAL_FROM_ISR()
#define link_unlock() taskEXIT_CRITICAL_FROM_ISR(link_lock_mask)

static struct {
    uint32_t inputs; // UHF task
    uint32_t turns;  // UHF task
    uint32_t pass;   // UHF task
    bool link_up;    // UHF task
    bool our_turn;   // UHF task

    uint32_t inputs_done; // CFDP task
    bool cfdp_done;       // CFDP task
} link;

void radio_link_set_up(bool up) {
    if (up) {
        // Nothing from the last pass may go out or be handled in this one. CFDP dropped its transaction when the link
        // went down, so nothing is still being queued
        if (downlink_queue != NULL) {
            xQueueReset(downlink_queue);
        }
        if (uplink_queue != NULL) {
            xQueueReset(uplink_queue);
        }
    }
    link_lock();
    if (up && !link.link_up) {
        link.pass++;
    }
    link.link_up = up;
    link.our_turn = false;
    link_unlock();
}

void radio_turn_started(void) {
    link_lock();
    link.turns++;
    link.inputs++; // CFDP must see the turn start (and queue any retransmits) before it counts as done
    link.our_turn = true;
    link_unlock();
}

void radio_turn_ended(void) {
    link_lock();
    link.our_turn = false;
    link_unlock();
}

void radio_cfdp_input_forwarded(void) {
    link_lock();
    link.inputs++;
    link_unlock();
}

radio_link_view_t radio_link_view(void) {
    link_lock();
    const radio_link_view_t view = {.inputs = link.inputs, .turns = link.turns, .pass = link.pass, .link_up = link.link_up};
    link_unlock();
    return view;
}

uint32_t radio_retransmit_turn(void) {
    link_lock();
    const uint32_t turn = link.turns + (link.our_turn ? 1 : 2);
    link_unlock();
    return turn;
}

void radio_cfdp_set_busy(void) {
    link_lock();
    link.cfdp_done = false;
    link_unlock();
}

void radio_cfdp_set_done(uint32_t inputs) {
    link_lock();
    const bool changed = !link.cfdp_done || link.inputs_done != inputs;
    link.inputs_done = inputs;
    link.cfdp_done = true;
    link_unlock();

    if (changed && p_uhf_radio_task->handle != NULL) {
        xTaskNotify(p_uhf_radio_task->handle, UHF_EVENT_CFDP_DONE, eSetBits);
    }
}

bool radio_cfdp_turn_done(void) {
    if (!p_cfdp_task->enabled) {
        return true;
    }
    link_lock();
    const bool done = link.cfdp_done && link.inputs_done == link.inputs;
    link_unlock();
    return done;
}
