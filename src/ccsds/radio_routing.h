/**
 * src/ccsds/radio_routing.h
 *
 * Which radio and USLP virtual channel each kind of data is sent or received on
 *
 * Created: 20261023 FRI
 * Updated: 20261023 FRI
 * Authors: Noah Shepard
 */

#ifndef PVDX_RADIO_ROUTING_H
#define PVDX_RADIO_ROUTING_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "ccsds/ccsds.h"
#include "tasks/uhf_radio/uhf_radio_task.h" // for radio_queue_item_t

#define RADIO_MAP_ID 0 // every stream uses MAP 0 of its virtual channel

// GMAP ID = TFVN (4) | SCID (16) | VCID (6) | MAP ID (4), see uslp_mapp_request
#define RADIO_GMAP_ID(vcid) (((uint32_t)USLP_TFVN << 26) | ((uint32_t)CCSDS_SCID << 10) | ((uint32_t)(vcid) << 4) | RADIO_MAP_ID)

typedef enum {
    RADIO_UHF = 0,
    RADIO_SBAND, // not implemented yet: anything routed here is rejected
} radio_id_t;

typedef enum {
    RADIO_STREAM_PEROVSKITE = 0, // VC 0, SBAND: perovskite readings, on request by uplink
    RADIO_STREAM_IMAGE,          // VC 1, SBAND: images, on request by uplink
    RADIO_STREAM_TELEMETRY,      // VC 2, UHF: telemetry, every ten minutes
    RADIO_STREAM_CFDP_REPLY,     // VC 3, UHF: CFDP ACK/NAK/FIN, whenever CFDP sends them
    RADIO_STREAM_UPLINK_CMD,     // VC 4, UHF: uplinked commands
    RADIO_STREAM_COUNT,          // not a stream
} radio_stream_t;

typedef struct {
    uint8_t vcid;
    radio_id_t radio;
} radio_route_t;

// Errors returned (negated) by radio_route_send
typedef enum {
    RADIO_OK = 0,
    RADIO_ERR_INVAL,
    RADIO_ERR_QUEUE_FULL,
    RADIO_ERR_NOT_IMPL,
} radio_err_t;

extern const radio_route_t radio_routes[RADIO_STREAM_COUNT];

/**
 * Queues a space packet for downlink on the radio and virtual channel of its stream
 *
 * \param stream - stream the packet belongs to
 * \param item - the packet to send, with len and data filled in; its vcid is set from the stream's route
 * \param wait - how long to wait for room in the radio's downlink queue
 *
 * \return RADIO_OK on success, else a negated radio_err_t
 *
 * \warning SBAND isn't implemented yet: packets for streams routed to it are discarded, but RADIO_OK is returned
 */
int radio_route_send(radio_stream_t stream, radio_queue_item_t *item, TickType_t wait);

/**
 * Whether frames received by radio on virtual channel vcid carry data the OS accepts
 */
bool radio_route_accepts_uplink(radio_id_t radio, uint8_t vcid);

#endif // PVDX_RADIO_ROUTING_H
