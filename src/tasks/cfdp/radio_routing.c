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

// Reference: radio_routing_info.md
const radio_route_t radio_routes[RADIO_STREAM_COUNT] = {
    [RADIO_STREAM_PEROVSKITE] = {.vcid = 0, .radio = RADIO_SBAND}, // on request by uplink
    [RADIO_STREAM_IMAGE] = {.vcid = 1, .radio = RADIO_SBAND},      // on request by uplink
    [RADIO_STREAM_TELEMETRY] = {.vcid = 2, .radio = RADIO_UHF},    // every ten minutes
    [RADIO_STREAM_CFDP_REPLY] = {.vcid = 3, .radio = RADIO_UHF},   // whenever CFDP sends an ACK/NAK/FIN
    [RADIO_STREAM_UPLINK_CMD] = {.vcid = 4, .radio = RADIO_UHF},   // when uplinked
};

// Streams the ground sends to us. Ground replies (ACK/NAK/FIN) to our downlinked files come in on the CFDP reply VC
static const radio_stream_t uplink_streams[] = {RADIO_STREAM_CFDP_REPLY, RADIO_STREAM_UPLINK_CMD};

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
