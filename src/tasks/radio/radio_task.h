#ifndef RADIO_TASK_H
#define RADIO_TASK_H

#include <atmel_start.h>
#include <driver_init.h>

#include "globals.h"

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

//Add functionality to handle case of data being pointed to yet not in radio?
#endif
