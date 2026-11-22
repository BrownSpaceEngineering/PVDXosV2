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
    RADIO_SBAND, // STXG2, downlink only. Used for every CFDP stream during an S-band pass
} radio_id_t;

// Which radio a stream uses depends on the pass the ground opened (PUSP_NOTIF_SBAND_PASS on its SYN): on an S-band pass
// every CFDP stream goes down on S-band, otherwise on UHF. Uplink is always UHF. Each stream keeps its VC on both radios
typedef enum {
    RADIO_STREAM_PEROVSKITE = 0, // VC 0: perovskite readings, on request by uplink
    RADIO_STREAM_IMAGE,          // VC 1: images, on request by uplink
    RADIO_STREAM_TELEMETRY,      // VC 2, UHF only: telemetry beacons, sent by the UHF task without CFDP
    RADIO_STREAM_CFDP_REPLY,     // VC 3: CFDP ACK/NAK/FIN, whenever CFDP sends them
    RADIO_STREAM_UPLINK_CMD,     // VC 4: uplinked commands, and CFDP's replies to them
    RADIO_STREAM_PUSP,           // VC 5, UHF only: PUSP directives, both ways, sent by the UHF task without CFDP
    RADIO_STREAM_COUNT,          // not a stream
} radio_stream_t;

typedef struct {
    uint8_t vcid;
    bool uhf_only; // Sent by the UHF task: never moves to S-band
} radio_route_t;

// Errors returned (negated) by radio_route_send
typedef enum {
    RADIO_OK = 0,
    RADIO_ERR_INVAL,
    RADIO_ERR_QUEUE_FULL,
    RADIO_ERR_SBAND, // The S-band stream isn't up or a write to it failed. See radio_sband_take_failure()
} radio_err_t;

extern const radio_route_t radio_routes[RADIO_STREAM_COUNT];

// The radio stream's packets go out on right now: S-band during an S-band pass unless the stream is UHF only, else UHF
radio_id_t radio_route_radio(radio_stream_t stream);

/**
 * Sends a space packet on the radio (radio_route_radio()) and virtual channel of its stream. UHF packets are queued for
 * the UHF task. S-band packets are framed and written to the STXG2 straight away (CFDP task only)
 *
 * \param stream - stream the packet belongs to
 * \param item - the packet to send, with len and data filled in; its vcid is set from the stream's route
 * \param wait - how long to wait for room in the UHF downlink queue
 *
 * \return RADIO_OK on success, else a negated radio_err_t
 */
int radio_route_send(radio_stream_t stream, radio_queue_item_t *item, TickType_t wait);

/**
 * Whether frames received by radio on virtual channel vcid carry data the OS accepts. Only UHF receives
 */
bool radio_route_accepts_uplink(radio_id_t radio, uint8_t vcid);

/* ---- UHF <-> CFDP link state ----
 *
 * Passes: a pass starts when the ground opens the link (LISTEN -> SYN_RECV) and ends when the UHF task goes back to
 * LISTEN, or when a SYN changes the pass type (UHF only / UHF + S-band), which starts a new pass straight away. CFDP
 * drops any transaction still running when its pass ends and only starts new ones while the link is up.
 *
 * Turns: CFDP's retransmits are counted in our turns (ESTABLISH_SEND), not wall-clock time, since nothing can be sent
 * or heard outside the matching turn. On an S-band pass we send whenever we like and the ground never yields, so a turn
 * there is RADIO_SBAND_TURN_MS of time instead.
 *
 * Yield: the UHF task yields its turn once CFDP has nothing more to send: its transaction is waiting on the ground
 * (CFDP_RESULT_BLOCKED), or it has none and no put waiting. CFDP only counts as done once it has seen every PDU and turn
 * start the UHF task gave it, so a "blocked" from before the ground's last PDUs can't end our turn before CFDP replies.
 * UHF_EVENT_CFDP_DONE only wakes the UHF task: its main loop clears its bits on every wake-up, so the state lives here.
 */

// On an S-band pass, how long counts as one of our turns for CFDP's retransmits. TODO: tune against the ground station
#define RADIO_SBAND_TURN_MS 5000

typedef struct {
    uint32_t inputs; // PDUs forwarded to CFDP plus turn starts, for radio_cfdp_set_done()
    uint32_t turns;  // Turns we've had since boot. On an S-band pass, one more every RADIO_SBAND_TURN_MS
    uint32_t pass;   // Passes since boot
    bool link_up;
    bool sband; // The pass is UHF + S-band. False while the link is down
} radio_link_view_t;

// UHF task: the link came up (a new pass, of the type sband says) or went down. A new pass empties the radio queues
void radio_link_set_up(bool up, bool sband);

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

/* ---- S-band stream. CFDP task only ----
 *
 * The STXG2 transmits for a whole S-band pass: powered on and streaming when CFDP first sees the pass, and off when the
 * pass ends or changes. If it can't be brought up, or a write fails, the stream is shut down and every S-band send fails
 * until a retry (RADIO_SBAND_RETRY_MS later) brings it back. CFDP kills the transaction that failed rather than falling
 * back to UHF: the ground never yields its UHF turn on an S-band pass, so it would never hear it.
 */

#define RADIO_SBAND_RETRY_MS 30000 // How long after a failure the S-band stream is brought up again, in the same pass

// Brings the S-band stream up or down to match the pass in view. Blocks for the bring-up (several seconds), checking in
// with the watchdog and stopping early if the CFDP task is being suspended or the pass ends
void radio_sband_service(const radio_link_view_t *view);

// Whether the S-band stream is up, so S-band sends can go out
bool radio_sband_ready(void);

// Whether an S-band bring-up or send has failed since the last call. Clears it
bool radio_sband_take_failure(void);

// Ends the S-band stream, if any, and powers the STXG2 off
void radio_sband_close(void);

/**
 * Wraps a queue item's payload in an SPP packet, written to buf
 *
 * \return true on failure, else false
 */
bool spp_wrap(const radio_queue_item_t *it, uint8_t *buf, size_t buf_size, size_t *packet_len);

#endif // PVDX_RADIO_ROUTING_H
