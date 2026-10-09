#ifndef UPLINK_COMMANDS_H
#define UPLINK_COMMANDS_H

#include <stddef.h>
#include <stdint.h>

typedef enum uplink_cmd_type {
    ENABLE_DEVICE = 0,
    DISABLE_DEVICE,
    SET_DEVICE_BROKEN,
    SET_DEVICE_UNBROKEN,
    UPDATE_DISPLAY,
    SLEEP,
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

typedef struct uplink_cmd {
    uplink_cmd_type_t cmd_type;
    size_t data_field_length;
    uint8_t data_field[];
} uplink_cmd_t;

typedef struct uplink_packet {
    uint8_t callsign[6];
    size_t message_size;
    uint16_t n_cmds;
    uint32_t timestamp;
    uplink_cmd_t cmds[];
} uplink_packet_t;

int parse_uplink_packet(const uint8_t *raw, uplink_packet_t *out, size_t n);
int parse_uplink_cmd(const uint8_t *raw, uplink_cmd_t *out, size_t n);

#endif // UPLINK_COMMANDS_H
