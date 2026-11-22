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
#include "cfdp_task.h"
#include "drivers/stxg2/stxg2.h"
#include "logging.h"
#include "queue.h"
#include "tasks/task_list.h"

// Reference: radio_routing_info.md
const radio_route_t radio_routes[RADIO_STREAM_COUNT] = {
    [RADIO_STREAM_PEROVSKITE] = {.vcid = 0, .uhf_only = false}, // on request by uplink
    [RADIO_STREAM_IMAGE] = {.vcid = 1, .uhf_only = false},      // on request by uplink
    [RADIO_STREAM_TELEMETRY] = {.vcid = 2, .uhf_only = true},   // UHF beacon bursts, without CFDP
    [RADIO_STREAM_CFDP_REPLY] = {.vcid = 3, .uhf_only = false}, // whenever CFDP sends an ACK/NAK/FIN
    [RADIO_STREAM_UPLINK_CMD] = {.vcid = 4, .uhf_only = false}, // when uplinked
    [RADIO_STREAM_PUSP] = {.vcid = 5, .uhf_only = true},        // UHF link handshakes, both ways
};

// TODO(temp): placeholder S-band RF settings. Frequency, symbol rate and MODCOD must match the ground station (and the
// licence). The STXG2 notes say MODCOD 0 or 1, but stxg2_configure_rf() only takes 1-28. Lowest PA power, which the
// flatsat's radio (14-20 dBm) rejects
#define SBAND_FREQ_KHZ 2450000u
#define SBAND_SYM_RATE_KSPS 5000u
#define SBAND_MODCOD 1u
#define SBAND_POWER_DBM STXG2_POWER_MIN_DBM

// TODO(temp): S-band framing, to be agreed with the ground decoder. The STXG2's framer is off, so each USLP frame is sent
// after a CCSDS attached sync marker for the ground to find it by, and filled out to a whole SPI word
#define SBAND_ASM 0x1ACFFC1DUL
#define SBAND_ASM_LEN 4u
#define SBAND_FILL 0x00u

// S-band framing buffers. Static, since a whole frame would take half of CFDP's stack. Only CFDP sends on S-band
static uint8_t sband_spp_buf[RADIO_DOWNLINK_MAX_SIZE];
static uint8_t sband_frame_buf[SBAND_ASM_LEN + USLP_MAX_FRAME_SIZE + 3];

typedef enum {
    SBAND_OFF = 0, // Powered off, nothing tried this pass
    SBAND_ON,      // Powered on and streaming
    SBAND_FAILED,  // Bring-up or a write failed. Powered off until sband_retry_at
} sband_state_t;

static sband_state_t sband_state = SBAND_OFF;
static uint32_t sband_pass = 0;       // Pass sband_state belongs to
static TickType_t sband_retry_at = 0; // When SBAND_FAILED tries again
static bool sband_failure = false;    // For radio_sband_take_failure()

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

// Shuts the stream down after a failure. Sends fail until the retry
static void sband_fail(void) {
    radio_sband_close();
    sband_state = SBAND_FAILED;
    sband_retry_at = xTaskGetTickCount() + pdMS_TO_TICKS(RADIO_SBAND_RETRY_MS);
    sband_failure = true;
}

/**
 * Frames a packet and writes it to the S-band stream: attached sync marker, USLP frame, fill to a whole word
 */
static int sband_send(const radio_queue_item_t *item) {
    if (sband_state != SBAND_ON) {
        warning("radio routing: S-band packet for APID 0x%x with the S-band stream down\n", item->apid);
        sband_failure = true;
        return -RADIO_ERR_SBAND;
    }

    size_t packet_len = 0;
    if (spp_wrap(item, sband_spp_buf, sizeof(sband_spp_buf), &packet_len)) {
        warning("radio routing: failed to wrap %u byte S-band packet for APID 0x%x\n", item->len, item->apid);
        return -RADIO_ERR_INVAL;
    }

    size_t frame_sz = USLP_MAX_FRAME_SIZE;
    if (uslp_mapp_request(sband_frame_buf + SBAND_ASM_LEN, &frame_sz, sband_spp_buf, (uint16_t)packet_len, RADIO_GMAP_ID(item->vcid),
                          SPP_VERSION_NUMBER, RADIO_SDU_ID, USLP_QOS_EXPEDITED)) {
        warning("radio routing: failed to build S-band USLP frame\n");
        return -RADIO_ERR_INVAL;
    }

    sband_frame_buf[0] = (uint8_t)(SBAND_ASM >> 24);
    sband_frame_buf[1] = (uint8_t)(SBAND_ASM >> 16);
    sband_frame_buf[2] = (uint8_t)(SBAND_ASM >> 8);
    sband_frame_buf[3] = (uint8_t)SBAND_ASM;
    size_t len = SBAND_ASM_LEN + frame_sz;
    while (len % 4u != 0) {
        sband_frame_buf[len++] = SBAND_FILL;
    }

    if (stxg2_stream_send(sband_frame_buf, len) != SUCCESS) {
        warning("radio routing: S-band write failed, shutting the stream down\n");
        sband_fail();
        return -RADIO_ERR_SBAND;
    }
    return RADIO_OK;
}

int radio_route_send(radio_stream_t stream, radio_queue_item_t *item, TickType_t wait) {
    if (stream >= RADIO_STREAM_COUNT || item == NULL || item->len == 0 || item->len > sizeof(item->data)) {
        return -RADIO_ERR_INVAL;
    }

    item->vcid = radio_routes[stream].vcid;

    switch (radio_route_radio(stream)) {
        case RADIO_UHF:
            if (xQueueSend(downlink_queue, item, wait) != pdPASS) {
                return -RADIO_ERR_QUEUE_FULL;
            }
            if (p_uhf_radio_task->handle != NULL) {
                xTaskNotify(p_uhf_radio_task->handle, UHF_EVENT_DOWNLINK, eSetBits);
            }
            return RADIO_OK;
        case RADIO_SBAND:
            return sband_send(item);
        default:
            return -RADIO_ERR_INVAL;
    }
}

bool radio_route_accepts_uplink(radio_id_t radio, uint8_t vcid) {
    if (radio != RADIO_UHF) {
        return false; // The STXG2 only transmits
    }
    for (size_t i = 0; i < sizeof(uplink_streams) / sizeof(uplink_streams[0]); i++) {
        if (radio_routes[uplink_streams[i]].vcid == vcid) {
            return true;
        }
    }
    return false;
}

// Run between the steps of the S-band bring-up. Stops it if the CFDP task is being suspended or the pass is over
static bool sband_step(void) {
    cfdp_checkin();
    const radio_link_view_t view = radio_link_view();
    return !cfdp_stop_requested() && view.link_up && view.sband && view.pass == sband_pass;
}

void radio_sband_service(const radio_link_view_t *view) {
    const bool wanted = view->link_up && view->sband;
    if (sband_state != SBAND_OFF && (!wanted || sband_pass != view->pass)) {
        info("radio routing: S-band pass %lu over, powering S-band off\n", (unsigned long)sband_pass);
        radio_sband_close();
    }
    if (!wanted || sband_state == SBAND_ON) {
        return;
    }
    if (sband_state == SBAND_FAILED && (int32_t)(xTaskGetTickCount() - sband_retry_at) < 0) {
        return;
    }

    info("radio routing: bringing S-band up for pass %lu\n", (unsigned long)view->pass);
    sband_pass = view->pass;
    status_t result = stxg2_init_step(sband_step);
    if (result == SUCCESS) {
        result = stxg2_stream_begin(SBAND_FREQ_KHZ, SBAND_SYM_RATE_KSPS, SBAND_MODCOD, SBAND_POWER_DBM, sband_step);
    }
    if (result != SUCCESS) {
        warning("radio routing: S-band bring-up failed (status %d), retrying in %d ms\n", result, RADIO_SBAND_RETRY_MS);
        sband_fail();
        return;
    }
    sband_state = SBAND_ON;
    info("radio routing: S-band streaming\n");
}

bool radio_sband_ready(void) {
    return sband_state == SBAND_ON;
}

bool radio_sband_take_failure(void) {
    const bool failed = sband_failure;
    sband_failure = false;
    return failed;
}

void radio_sband_close(void) {
    if (sband_state == SBAND_ON && stxg2_stream_end() != SUCCESS) {
        warning("radio routing: S-band stream end failed, powering off anyway\n");
    }
    if (sband_state != SBAND_OFF) {
        stxg2_power_off();
    }
    sband_state = SBAND_OFF;
}

// Link state. Each field is written by one task only, and all are read and written under link_lock() so they're
// consistent with each other. The _FROM_ISR critical sections save and restore the interrupt mask instead of counting
// nesting, so they're also safe before the scheduler starts (the PUSP tests change UHF states then)
#define link_lock() UBaseType_t link_lock_mask = taskENTER_CRITICAL_FROM_ISR()
#define link_unlock() taskEXIT_CRITICAL_FROM_ISR(link_lock_mask)

static struct {
    uint32_t inputs;        // UHF task
    uint32_t turns;         // UHF task
    uint32_t pass;          // UHF task
    bool link_up;           // UHF task
    bool sband;             // UHF task
    bool our_turn;          // UHF task
    TickType_t sband_start; // UHF task: when the S-band pass started, for its timed turns

    uint32_t inputs_done; // CFDP task
    bool cfdp_done;       // CFDP task
} link;

// Turns so far, counting an S-band pass's timed ones. Call under link_lock()
static uint32_t link_turns_locked(void) {
    if (!link.link_up || !link.sband) {
        return link.turns;
    }
    return link.turns + (uint32_t)((xTaskGetTickCount() - link.sband_start) / pdMS_TO_TICKS(RADIO_SBAND_TURN_MS));
}

radio_id_t radio_route_radio(radio_stream_t stream) {
    if (stream >= RADIO_STREAM_COUNT || radio_routes[stream].uhf_only) {
        return RADIO_UHF;
    }
    link_lock();
    const bool sband = link.link_up && link.sband;
    link_unlock();
    return sband ? RADIO_SBAND : RADIO_UHF;
}

void radio_link_set_up(bool up, bool sband) {
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
    // Keep the S-band pass's timed turns, so turns never go backwards
    link.turns = link_turns_locked();
    if (up && !link.link_up) {
        link.pass++;
    }
    link.link_up = up;
    link.sband = up && sband;
    link.sband_start = xTaskGetTickCount();
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
    const radio_link_view_t view = {
        .inputs = link.inputs, .turns = link_turns_locked(), .pass = link.pass, .link_up = link.link_up, .sband = link.sband};
    link_unlock();
    return view;
}

uint32_t radio_retransmit_turn(void) {
    link_lock();
    // On an S-band pass the PDU goes out now, so two timed turns give the ground at least a whole one to answer
    const uint32_t turn = link_turns_locked() + ((link.our_turn && !link.sband) ? 1 : 2);
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
