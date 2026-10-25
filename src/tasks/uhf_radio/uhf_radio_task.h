#ifndef UHF_RADIO_TASK_H
#define UHF_RADIO_TASK_H

#include <atmel_start.h>
#include <driver_init.h>

#include "FreeRTOS.h"
#include "ccsds/ccsds.h"
#include "ccsds/spp.h"
#include "globals.h"
#include "queue.h"

#define UHF_RADIO_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)
#define RADIO_DOWNLINK_MAX_SIZE (SPP_PRIMARY_HEADER_SIZE + SPP_SECONDARY_HEADER_SIZE + 512)

// How long each loop waits for a packet to send before polling the radio for received frames
#define UHF_RADIO_POLL_MS 10

#define RADIO_DOWNLINK_QUEUE_LENGTH 4 // TODO: How long should queue be?
#define RADIO_UPLINK_QUEUE_LENGTH 4
#define RADIO_SCID CCSDS_SCID // Spacecraft id
#define RADIO_SDU_ID 0

#define UHF_EVENT_DOWNLINK (1u << 0)
#define UHF_EVENT_RXFE (1u << 1)
#define UHF_EVENT_TXFE (1u << 2)
#define UHF_EVENT_TXRDY (1u << 3)

typedef struct {
    uint16_t len;
    uint8_t vcid;                          // USLP virtual channel the packet is sent or was received on, see radio_routing.h
    uint8_t data[RADIO_DOWNLINK_MAX_SIZE]; // SPP header followed by the CFDP PDU
} radio_queue_item_t;

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

// How long a radio state change may take before it is treated as failed
#define AT86_STATE_CHANGE_TIMEOUT_MS 10
// How long one frame may take to transmit. A full AT86RF215_MAX_PDU frame takes about 2.6 s at 6.25 kb/s
#define AT86_TX_TIMEOUT_MS 3000

void uhf_rx_process();
bool uhf_tx_process();

void uhf_go_rx();
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

int set_at86_rx_mode(struct at86rf215 *h, size_t timeout_ms);

int set_at86_tx_prep_mode(struct at86rf215 *h, size_t timeout_ms);

int change_at86_config(struct at86rf215 *h, at86_config_t config);

int at86_send_frame(struct at86rf215 *h, const uint8_t *buf, size_t sz, size_t timeout_ms);

bool at86_peek_frame(void);

int at86_pop_frame(uint8_t *buf, size_t sz);

int at86_check_health(struct at86rf215 *h, at86_health_t *health);

// Add functionality to handle case of data being pointed to yet not in radio?
#endif
