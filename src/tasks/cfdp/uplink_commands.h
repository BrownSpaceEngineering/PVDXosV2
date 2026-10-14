#ifndef UPLINK_COMMANDS_H
#define UPLINK_COMMANDS_H

#include <stddef.h>
#include <stdint.h>

#include "cfdp_task.h"

typedef enum uplink_cmd_type {
    ENABLE_DISABLE_DEVICE = 0,
    SET_DEVICE_BROKEN_UNBROKEN,
    UPDATE_DISPLAY,
    SLEEP_TRANSMIT,
    DISABLE_TRANSMIT,
    REBOOT,
    TAKE_PHOTO,
    SEND_PHOTO,
    SET_KEPLER_COEFF,
    SET_UNIX_TIME,
    SET_ADCS_OP_MODE,
    ADCS_UPDATE_PARAMS,
    SET_PWR_MODE
} uplink_cmd_type_e;

typedef uint16_t uplink_cmd_type_t;

typedef struct uplink_packet {
    uint8_t callsign[6];
    size_t message_size;
    uint16_t n_cmds;
    uint32_t timestamp;
    uplink_cmd_t cmds[];
} uplink_packet_t;

int parse_uplink_packet(cfdp_buff_header_t *buff, size_t n);
int parse_uplink_cmd(cfdp_buff_header_t *buff, size_t offset, size_t n);

#endif // UPLINK_COMMANDS_H
