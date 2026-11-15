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
            // TODO: hand the packet to the SBAND task once it exists. Until then SBAND packets are discarded but reported as
            // sent, so SBAND transactions run as normal on our side (no SBAND transactions are expected yet)
            debug("radio routing: discarding packet for stream %d, SBAND is not implemented\n", stream);
            return RADIO_OK;
        default:
            return -RADIO_ERR_INVAL;
    }
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
