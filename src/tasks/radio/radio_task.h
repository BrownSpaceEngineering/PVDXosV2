#ifndef RADIO_TASK_H
#define RADIO_TASK_H

#include <atmel_start.h>
#include <driver_init.h>

#include "FreeRTOS.h"
#include "ccsds/ccsds.h"
#include "ccsds/spp.h"
#include "globals.h"
#include "queue.h"

#define RADIO_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)
#define RADIO_DOWNLINK_MAX_SIZE (SPP_PRIMARY_HEADER_SIZE + SPP_SECONDARY_HEADER_SIZE + 512)

#define RADIO_DOWNLINK_QUEUE_LENGTH 4 // TODO: How long should queue be?
#define RADIO_UPLINK_QUEUE_LENGTH 4
#define RADIO_SCID CCSDS_SCID   // Spacecraft id
#define RADIO_DOWNLINK_VCID 0   // Virtual Channel
#define RADIO_DOWNLINK_MAP_ID 0 // Map ID
#define RADIO_GMAP_ID (((uint32_t)USLP_TFVN << 26) | ((uint32_t)RADIO_SCID << 10) | (RADIO_DOWNLINK_VCID << 4) | RADIO_DOWNLINK_MAP_ID)
#define RADIO_SDU_ID 0

typedef struct {
    uint16_t len;
    uint8_t data[RADIO_DOWNLINK_MAX_SIZE]; // SPP header followed by the CFDP PDU
} radio_queue_item_t;

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t radio_task_stack[RADIO_TASK_STACK_SIZE];
    uint8_t radio_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    uint8_t downlink_queue_buffer[RADIO_DOWNLINK_QUEUE_LENGTH * sizeof(radio_queue_item_t)];
    uint8_t uplink_queue_buffer[RADIO_UPLINK_QUEUE_LENGTH * sizeof(radio_queue_item_t)];
    StaticQueue_t radio_task_queue;
    StaticQueue_t uplink_queue_mem;
    StaticQueue_t downlink_queue_mem;
    StaticTask_t radio_task_tcb;
} radio_task_memory_t;

extern QueueHandle_t uplink_queue;
extern QueueHandle_t downlink_queue;

// Global memory and configuration
extern radio_task_memory_t radio_mem;

#define RADIO_BUFFER_SIZE 128

typedef struct {
    uint8_t target;
    uint8_t operation;
    char data[8]; // TODO: We need to decide how to represent the data to send up images.
    // Should "packeting" be done on the main board, or on the UHF board?
} uplink_data_t;

typedef struct {
    uint8_t target;
    uint8_t operation;
    uint8_t status;
    uint8_t data_type;
    char *data;
} downlink_data_t;

typedef enum {
    MR_O_QPSK

} at86_config_t;

QueueHandle_t init_radio(void);
void main_radio(void *pvParameters);

typedef struct at86_config at86_config_t;
typedef struct at86_health at86_health_t;

bool radio_transmit(const uint8_t *frame, size_t len);

int set_at86_rx_mode();

int set_at86_tx_mode();

int change_at86_config(at86_config_t *config);

int at86_send_frame(uint8_t *buf, size_t sz);

bool at86_peek_frame();

int at86_pop_frame(uint8_t *buf, size_t sz);

int at86_check_health(at86_health_t *health);

// Add functionality to handle case of data being pointed to yet not in radio?
#endif
