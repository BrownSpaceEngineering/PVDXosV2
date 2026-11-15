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
    RADIO_STREAM_TELEMETRY,      // VC 2, UHF: telemetry beacons, sent by the UHF task without CFDP
    RADIO_STREAM_CFDP_REPLY,     // VC 3, UHF: CFDP ACK/NAK/FIN, whenever CFDP sends them
    RADIO_STREAM_UPLINK_CMD,     // VC 4, UHF: uplinked commands
    RADIO_STREAM_PUSP,           // VC 5, UHF: PUSP directives, both ways, sent by the UHF task without CFDP
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

/* ---- UHF <-> CFDP link state ----
 *
 * Passes: a pass starts when the ground opens the link (LISTEN -> SYN_RECV) and ends when the UHF task goes back to
 * LISTEN. CFDP drops any transaction still running when its pass ends and only starts new ones while the link is up.
 *
 * Turns: CFDP's retransmits are counted in our turns (ESTABLISH_SEND), not wall-clock time, since nothing can be sent
 * or heard outside the matching turn.
 *
 * Yield: the UHF task yields its turn once CFDP has nothing more to send: its transaction is waiting on the ground
 * (CFDP_RESULT_BLOCKED), or it has none and no put waiting. CFDP only counts as done once it has seen every PDU and turn
 * start the UHF task gave it, so a "blocked" from before the ground's last PDUs can't end our turn before CFDP replies.
 * UHF_EVENT_CFDP_DONE only wakes the UHF task: its main loop clears its bits on every wake-up, so the state lives here.
 */

typedef struct {
    uint32_t inputs; // PDUs forwarded to CFDP plus turn starts, for radio_cfdp_set_done()
    uint32_t turns;  // Turns we've had since boot
    uint32_t pass;   // Passes since boot
    bool link_up;
} radio_link_view_t;

// UHF task: the link came up (a new pass) or went down. A new pass empties the radio queues
void radio_link_set_up(bool up);

// UHF task: our turn started or ended
void radio_turn_started(void);
void radio_turn_ended(void);

// UHF task: call after putting a PDU on the uplink queue
void radio_cfdp_input_forwarded(void);

// CFDP task: snapshot of the link. Take it before reading the uplink queue, and pass inputs to radio_cfdp_set_done()
radio_link_view_t radio_link_view(void);

// CFDP task: the first turn by the end of which the ground has had a full turn to answer a PDU queued now. That's the
// next turn if we're in our turn, else the one after, since the PDU only goes out in our next turn
uint32_t radio_retransmit_turn(void);

// CFDP task: call before queueing PDUs that no input prompted (a put starting, a timeout, a cancel)
void radio_cfdp_set_busy(void);

// CFDP task: everything CFDP can send in reply to the inputs counted in inputs is queued. Wakes the UHF task
void radio_cfdp_set_done(uint32_t inputs);

// UHF task: whether CFDP is done and has seen every input given to it. Always true if the CFDP task is disabled
bool radio_cfdp_turn_done(void);

#endif // PVDX_RADIO_ROUTING_H
