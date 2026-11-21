#ifndef UHF_RADIO_TASK_H
#define UHF_RADIO_TASK_H

#include <atmel_start.h>
#include <driver_init.h>

#include "FreeRTOS.h"
#include "ccsds/ccsds.h"
#include "ccsds/spp.h"
#include "globals.h"
#include "pusp.h"
#include "queue.h"

#define UHF_RADIO_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)
#define RADIO_PAYLOAD_MAX_SIZE 512     // Largest SPP user data field (e.g. one CFDP PDU) the radio task sends or receives
#define RADIO_DOWNLINK_MAX_SIZE (SPP_PRIMARY_HEADER_SIZE + SPP_SECONDARY_HEADER_SIZE + RADIO_PAYLOAD_MAX_SIZE) // Whole space packet

// Longest the main loop blocks without an event or deadline, so watchdog check-ins stay on time
#define UHF_RADIO_WAIT_MS 1000
// Shortest time between watchdog check-ins. The radio task's watchdog timeout is in task_list.c
#define UHF_CHECKIN_PERIOD_MS 1000

// TODO(temp): placeholder RF settings, copied from the burn sequence except for the PHY (the burn sequence uses its own
// link config, which the radio task doesn't have). All three must match the ground station and be confirmed on hardware
#define UHF_FREQ_HZ 437765000UL
#define UHF_TX_POWER 10 // RF09_PAC.TXPWR, 0-31 in 1 dB steps. Not yet related to the real output power of the board
#define UHF_PHY_CONFIG AT86_MR_FSK

#define UHF_STATE_TIMEOUT_MS 50 // How long uhf_chip_setup() waits for the radio to change state
#define UHF_SETUP_RETRY_MS 5000 // How long to wait before retrying a failed uhf_chip_setup(). Less than the watchdog timeout

#define RADIO_DOWNLINK_QUEUE_LENGTH 4 // TODO: How long should queue be?
#define RADIO_UPLINK_QUEUE_LENGTH 4
#define RADIO_SCID CCSDS_SCID // Spacecraft id
#define RADIO_SDU_ID 0

#define UHF_EVENT_DOWNLINK (1u << 0)
#define UHF_EVENT_IRQ (1u << 1) // Set by uhf_radio_irq_handler(), the task then calls uhf_read_irqs()
// Set by uhf_irqs_to_events(). After RXFE or TXFE the radio is in TXPREP
#define UHF_EVENT_RXFE (1u << 2)      // A frame was received
#define UHF_EVENT_TXFE (1u << 3)      // A frame was sent
#define UHF_EVENT_TXRDY (1u << 4)     // The radio reached TXPREP. Masked from the pin, so only seen alongside another IRQ
#define UHF_EVENT_CFDP_DONE (1u << 5) // CFDP has nothing more to send for now, see radio_cfdp_turn_done()

// PUSP link timing. TODO: tune on hardware. The retry period must cover a whole round trip: our frame's airtime, the
// ground station's RX/TX turnaround and its reply's airtime
#define UHF_RETRY_MS 1000          // How long a handshake state waits for a reply before re-sending
#define UHF_MAX_RETRIES 5          // Re-sends before a handshake state gives up and goes back to LISTEN
#define UHF_LINK_TIMEOUT_MS 30000  // A link with no valid packet from the ground for this long drops back to LISTEN
#define UHF_TX_WINDOW_MS 10000     // Longest ESTABLISH_SEND keeps the turn before yielding, if CFDP never says it's done
#define UHF_BEACON_PERIOD_MS 60000 // Time from the end of one beacon burst to the start of the next
#define UHF_BEACON_BURST_MS 10000  // How long BEACON keeps re-sending the beacon frame
// Longest a polled transmit waits for its frame to go out. DATASHEET(verify): assumes the MR-FSK config's RSC FEC is rate
// 1/2, so a ~540 byte beacon frame at 50 ksym/s takes ~175 ms on air. Confirm the real airtime on hardware
#define UHF_TX_TIMEOUT_MS 500
_Static_assert(UHF_TX_WINDOW_MS < UHF_LINK_TIMEOUT_MS, "ESTABLISH_SEND receives nothing, so it must yield before the link times out");

// IRQ status of the sub-1 GHz radio and baseband core
typedef struct {
    uint8_t rf;  // RF09_IRQS
    uint8_t bbc; // BBC0_IRQS
} uhf_irqs_t;

// The radio task adds the SPP headers on downlink and strips them on uplink, so data is only the SPP user data field
typedef struct {
    uint16_t len;
    uint16_t apid;                        // SPP APID of the packet, see CCSDS_APID_* in ccsds.h
    uint8_t vcid;                         // USLP virtual channel the packet is sent or was received on, see radio_routing.h
    uint8_t data[RADIO_PAYLOAD_MAX_SIZE]; // SPP user data field, e.g. one CFDP PDU
} radio_queue_item_t;

typedef enum uhf_state {
    BEACON = 0,
    LISTEN = 1,
    SYN_RECV = 2,
    ESTABLISH_RECV = 3,
    TRANS_TO_TX = 4,
    ESTABLISH_SEND = 5,
    TRANS_TO_RX = 6,
    UHF_STATE_COUNT
} uhf_state_t;

// What uhf_rx_poll() found in a received frame. It parses and checks the frame, but forwarding is up to the state
typedef enum {
    UHF_RX_NONE = 0, // No frame this iteration, or it was dropped
    UHF_RX_CFDP,     // item holds a CFDP PDU
    UHF_RX_PUSP,     // dir holds a PUSP directive
} uhf_rx_kind_t;

typedef struct {
    uhf_rx_kind_t kind;
    pusp_directive_t dir;           // Valid when kind == UHF_RX_PUSP
    uint8_t notifs;                 // PUSP_NOTIF_* flags, valid when kind == UHF_RX_PUSP
    const radio_queue_item_t *item; // Valid when kind == UHF_RX_CFDP, ready to put on the uplink queue
} uhf_rx_t;

typedef struct uhf_ctx uhf_ctx_t;

/**
 * Handles one main loop iteration in a state: everything the state does, including its own timeouts. Runs every
 * iteration, whether or not anything happened.
 *
 * \param ctx task state. Handlers may change deadlines in it but don't change ctx->state themselves
 * \param rx  the frame received this iteration, if any
 * \return the next state. Returning a different state makes the main loop call uhf_enter_state()
 */
typedef uhf_state_t (*uhf_state_proc_t)(uhf_ctx_t *ctx, const uhf_rx_t *rx);

// Per-state policy, used by uhf_enter_state() and uhf_retry(). A state that runs out of retries goes back to LISTEN
typedef struct {
    uhf_state_proc_t proc;
    uint16_t retry_ms;   // Period of the retry deadline, 0 for no retry timer in this state
    uint8_t max_retries; // Re-sends uhf_retry() makes before giving up
} uhf_state_desc_t;

// Radio task state
struct uhf_ctx {
    uhf_state_t state;
    TickType_t retry_deadline; // Absolute tick; armed by uhf_enter_state() when the state has a retry_ms
    uint8_t retries;           // Re-sends so far in this state, reset by uhf_enter_state()
    TickType_t last_rx;        // Tick of the last valid packet from the ground, for UHF_LINK_TIMEOUT_MS
    TickType_t next_beacon;    // Absolute tick at which LISTEN goes to BEACON
    TickType_t beacon_end;     // Absolute tick at which BEACON stops sending and goes back to LISTEN, set by LISTEN
    TickType_t yield_deadline; // Absolute tick at which ESTABLISH_SEND yields, set on entering it
};

extern const uhf_state_desc_t uhf_state_descs[UHF_STATE_COUNT];

// ---- PUSP helpers. uhf_state.c, except where noted ----

/**
 * If events has UHF_EVENT_IRQ, reads the radio IRQs. On RXFE, reads the frame from the BBC0 RX buffer, checks it (FCS,
 * USLP frame, VC, telecommand, ground callsign, APID matches VC), classifies it by SPP APID, puts the radio back in RX
 * and, for a valid packet, sets ctx->last_rx. Forwards nothing: the state handler decides (uhf_forward_cfdp()). A CFDP
 * rx.item points at a static that the next call overwrites (uhf_radio_task.c)
 *
 * \return the classified frame, kind UHF_RX_NONE if there was none or it was dropped (the reason is logged)
 */
// DATASHEET(verify): RXFE moves the radio from RX to TXPREP by itself, without a TRXRDY IRQ (5.2.7, 5.3.2.4 IRQS.RXFE)
uhf_rx_t uhf_rx_poll(uhf_ctx_t *ctx, uint32_t events);

// True if rx is the PUSP directive dir (uhf_radio_task.c)
bool uhf_rx_is_pusp(const uhf_rx_t *rx, pusp_directive_t dir);

/**
 * Acts on the PUSP_NOTIF_* flags of a directive from the ground: PUSP_NOTIF_DISABLE_TX / PUSP_NOTIF_ENABLE_TX turn
 * transmitting off and on. Both at once is ignored (logged), as are unknown bits. Transmit starts enabled at boot, so a
 * reset re-enables it (uhf_radio_task.c)
 *
 * \return true if this changed whether transmit is enabled
 */
bool dispatch_pusp_immediate_cmd(uint8_t notifs);

// False while the ground has disabled transmit. uhf_step() then keeps the task in LISTEN, and every transmit is
// refused (uhf_radio_task.c)
bool uhf_tx_is_enabled(void);

// Puts the CFDP PDU in rx on the uplink queue and counts it for radio_cfdp_turn_done(), warning if the queue is full.
// Does nothing for other kinds (uhf_radio_task.c)
void uhf_forward_cfdp(const uhf_rx_t *rx);

/**
 * Wraps a PUSP directive in SPP (CCSDS_APID_PUSP) and USLP (RADIO_STREAM_PUSP's VC) and sends it: TXPREP, send, poll
 * until the frame is out, then back to RX.
 *
 * Records dir in uhf_pusp_tx_last/uhf_pusp_tx_count first. Until uhf_chip_setup() has succeeded it sends nothing and
 * returns false: that's the seam the PUSP tests use, since they run before the radio is set up.
 *
 * \return true if the frame was sent (failures are logged). The radio is left in RX either way (uhf_radio_task.c)
 */
bool uhf_transmit_pusp(pusp_directive_t dir);

// The last directive uhf_transmit_pusp() was asked to send, and how many it has been asked to send since boot
extern pusp_directive_t uhf_pusp_tx_last;
extern uint32_t uhf_pusp_tx_count;

/**
 * Builds the beacon frame for the next burst: collects telemetry, serializes it and wraps it in SPP (APID
 * CCSDS_APID_TELEMETRY) and USLP (the telemetry stream's VC). Every frame of a burst is this same frame.
 *
 * \return true on success, false if the frame couldn't be built (the reason is logged)
 *
 * (uhf_radio_task.c)
 */
bool uhf_beacon_prepare(void);

/**
 * Sends the frame built by uhf_beacon_prepare() once: TXPREP, send, poll until the frame is out. Leaves the radio in
 * TXPREP, so the next beacon frame can go straight out; going back to RX is up to the caller.
 *
 * \return true if the frame was sent, false otherwise (the reason is logged)
 *
 * (uhf_radio_task.c)
 */
bool uhf_transmit_beacon(void);

/**
 * Moves the task to next and logs it: resets retries, arms retry_deadline from uhf_state_descs[next] (if it has a
 * retry_ms) and sets yield_deadline when entering ESTABLISH_SEND. Re-entering the current state
 * counts as a fresh entry.
 */
void uhf_enter_state(uhf_ctx_t *ctx, uhf_state_t next);

/**
 * One main loop iteration after polling: acts on rx's PUSP notification flags, then runs the current state's handler and
 * enters the state it returns. While transmit is disabled, any other state is dropped to LISTEN (radio in RX) instead
 * of running its handler, so a link or beacon burst can't send anything.
 *
 * \return the state the task is in afterwards
 */
uhf_state_t uhf_step(uhf_ctx_t *ctx, const uhf_rx_t *rx);

// Sets retry_deadline to now + the current state's retry_ms, without touching retries. Used after a re-send prompted by
// a duplicate so the retry timer doesn't re-send again straight after
void uhf_rearm_retry(uhf_ctx_t *ctx);

/**
 * The retry timer of a state waiting for a reply to dir. If retry_deadline has passed: after the state's max_retries
 * re-sends, gives up (logged); otherwise re-sends dir and re-arms the deadline.
 *
 * \return LISTEN on giving up, else ctx->state
 */
uhf_state_t uhf_retry(uhf_ctx_t *ctx, pusp_directive_t dir);

/**
 * Checks shared by every link state, which call it first. A SYN means the ground restarted the link: sends SYN_ACK
 * (re-arming the retry timer if already in SYN_RECV). No packet from the ground for UHF_LINK_TIMEOUT_MS drops the link
 * (logged).
 *
 * \return SYN_RECV on a SYN, LISTEN on a timeout, else ctx->state: the handler carries on only if this is ctx->state
 */
uhf_state_t uhf_link_check(uhf_ctx_t *ctx, const uhf_rx_t *rx);

// True once deadline has passed. Compares (int32_t)(now - deadline) >= 0, so tick wraparound is harmless
bool uhf_deadline_passed(TickType_t deadline);

/**
 * How long the main loop may block: until the earliest deadline that matters in ctx->state (retry_deadline, next_beacon
 * in LISTEN, yield_deadline in ESTABLISH_SEND, the link timeout in link states), capped so watchdog check-ins
 * still happen on time. 0 if a deadline has already passed, and always 0 in BEACON, which sends back to back.
 */
TickType_t uhf_ticks_until_wake(const uhf_ctx_t *ctx);

// Checks in with the watchdog if the task is enabled and UHF_CHECKIN_PERIOD_MS has passed since the last check-in, so
// it's safe to call as often as wanted (uhf_radio_main.c)
void uhf_watchdog_checkin(void);

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t uhf_radio_task_stack[UHF_RADIO_TASK_STACK_SIZE];
    uint8_t uhf_radio_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    uint8_t downlink_queue_buffer[RADIO_DOWNLINK_QUEUE_LENGTH * sizeof(radio_queue_item_t)];
    uint8_t uplink_queue_buffer[RADIO_UPLINK_QUEUE_LENGTH * sizeof(radio_queue_item_t)];
    StaticQueue_t uhf_radio_task_queue;
    StaticQueue_t uplink_queue_mem;
    StaticQueue_t downlink_queue_mem;
    StaticTask_t uhf_radio_task_tcb;
} uhf_radio_task_memory_t;

extern QueueHandle_t uplink_queue;
extern QueueHandle_t downlink_queue;

// Global memory and configuration
extern uhf_radio_task_memory_t uhf_radio_mem;

uhf_state_t beacon_proc(uhf_ctx_t *ctx, const uhf_rx_t *rx);
uhf_state_t listen_proc(uhf_ctx_t *ctx, const uhf_rx_t *rx);
uhf_state_t syn_recv_proc(uhf_ctx_t *ctx, const uhf_rx_t *rx);
uhf_state_t establish_recv_proc(uhf_ctx_t *ctx, const uhf_rx_t *rx);
uhf_state_t establish_send_proc(uhf_ctx_t *ctx, const uhf_rx_t *rx);
uhf_state_t transition_to_tx_proc(uhf_ctx_t *ctx, const uhf_rx_t *rx);
uhf_state_t transition_to_rx_proc(uhf_ctx_t *ctx, const uhf_rx_t *rx);

void at86_irq_setup(void);
void uhf_radio_irq_handler(void);
uhf_irqs_t uhf_read_irqs(void);
uint32_t uhf_irqs_to_events(uhf_irqs_t irqs);

bool uhf_tx_process();

/**
 * Resets the chip and configures it for this task, leaving the radio listening. Needs the scheduler running, since the
 * driver's delays use vTaskDelay. Can be called again to retry after a failure.
 *
 * \return true on success, false if the chip couldn't be set up (the reason is logged)
 */
bool uhf_chip_setup(void);

int uhf_go_rx();
void uhf_go_txprep();

/**
 * Valid PHY configs
 *
 * We specifically support MR-O-QPSK modes at 100K Chip/s and 6.25 kb/s or 12.5 kb/s
 * We also have support for legacy O-QPSK, though this is primarily because it is
 * well-supported with existing GNU Radio demodulators
 *
 * TODO: AT86_MR_O_QPSK_100CH_6_2_5KB is 100 kchip/s, not 6.25 kchip/s (datasheet Table 6-100)
 */
typedef enum {
    AT86_O_QPSK_LEG = 0,              // Used for testing, 250 kb/s
    AT86_MR_O_QPSK_100CH_6_2_5KB = 1, // 6.25 KChip/s, 6.25 kb/s
    AT86_MR_O_QPSK_100CH_12_5KB = 2,  // 100 KChip/s, 12.5 kb/s
    AT86_MR_FSK = 3                   // 2-GFSK, BT = 0.5, h = 0.5, 50 ksymbol/s (50 kb/s uncoded), RSC FEC
} at86_config_t;

QueueHandle_t init_uhf_radio(void);
void main_uhf_radio(void *pvParameters);

typedef struct {
    uint8_t part_number; // RF_PN (0x000D): 0x34 for the AT86RF215
    uint8_t version;     // RF_VN (0x000E): chip revision
    uint8_t rf09_state;  // RF09_STATE (0x0102): sub-1GHz state, e.g. 0x2 = TRXOFF, 0x6 = TRANSITION
} at86_health_t;

int change_at86_config(struct at86rf215 *h, at86_config_t config);

int at86_send_frame(struct at86rf215 *h, const uint8_t *buf, size_t sz);

int at86_check_health(struct at86rf215 *h, at86_health_t *health);

// Add functionality to handle case of data being pointed to yet not in radio?
#endif
