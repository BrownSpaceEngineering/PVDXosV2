#include "uplink_parse.h"

#include <string.h>

#include "globals.h"

_Static_assert(sizeof(UPLINK_CALLSIGN) - 1 == UPLINK_CALLSIGN_LENGTH, "UPLINK_CALLSIGN must be UPLINK_CALLSIGN_LENGTH chars");

// In command_data_sizes, a command whose data can be any size
#define UPLINK_ANY_SIZE UINT32_MAX

// Expected command data size, in bytes, for each command type
static const uint32_t command_data_sizes[UPLINK_CMD_COUNT] = {
    [ENABLE_DISABLE_DEVICE] = 2,
    [SET_DEVICE_BROKEN_UNBROKEN] = 2,
    [UPDATE_DISPLAY] = UPLINK_DISPLAY_BITMAP_SIZE,
    [SLEEP_TRANSMIT] = 4,
    [DISABLE_TRANSMIT] = UPLINK_ANY_SIZE, // TODO: not in the uplink spreadsheet yet
    [REBOOT] = 1,
    [TAKE_PHOTO] = 4,
    [SEND_PHOTO] = 4,
    [SET_KEPLER_COEFF] = 4 * UPLINK_KEPLER_COEFFICIENTS,
    [SET_UNIX_TIME] = 4,
    [SET_ADCS_OP_MODE] = 2,
    [ADCS_UPDATE_PARAMS] = 4 + 4 * 6,
    [SET_PWR_MODE] = 2,
};

/* ---------- READING ---------- */

// Reads values out of a buffer, big-endian. Once a read doesn't fit, every read returns 0 and overflow stays set,
// so a whole sequence of reads can be checked once at the end
typedef struct {
    const uint8_t *buf;
    size_t size;
    size_t offset;
    bool overflow;
} uplink_reader_t;

/**
 * Consumes n bytes from r
 *
 * \return pointer to the consumed bytes, or NULL if there aren't n left
 */
static const uint8_t *consume(uplink_reader_t *r, size_t n) {
    if (r->overflow || r->size - r->offset < n) {
        r->overflow = true;
        return NULL;
    }
    const uint8_t *p = &r->buf[r->offset];
    r->offset += n;
    return p;
}

static uint8_t get_uint8(uplink_reader_t *r) {
    const uint8_t *p = consume(r, 1);
    return p ? p[0] : 0;
}

static uint16_t get_uint16(uplink_reader_t *r) {
    const uint8_t *p = consume(r, 2);
    return p ? (uint16_t)((p[0] << 8) | p[1]) : 0;
}

static uint32_t get_uint32(uplink_reader_t *r) {
    const uint8_t *p = consume(r, 4);
    return p ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3] : 0;
}

static float get_float(uplink_reader_t *r) {
    uint32_t bits = get_uint32(r);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* ---------- PARSING ---------- */

/**
 * Parses one command (type, data size and data) from r into cmd
 *
 * \return true on failure (out of bytes, unknown type, wrong data size, or invalid data), else false
 */
static bool parse_command(uplink_reader_t *r, uplink_parsed_cmd_t *cmd) {
    cmd->type = get_uint16(r);
    cmd->data_size = get_uint32(r);
    if (r->overflow || cmd->type >= UPLINK_CMD_COUNT) {
        return true;
    }
    uint32_t expected_size = command_data_sizes[cmd->type];
    if (expected_size != UPLINK_ANY_SIZE && cmd->data_size != expected_size) {
        return true;
    }

    cmd->data = consume(r, cmd->data_size);
    if (cmd->data == NULL) {
        return true; // the command's data runs past the end of the uplink
    }

    // Decode the data, which we know is exactly the right size
    uplink_reader_t data = {.buf = cmd->data, .size = cmd->data_size};
    switch (cmd->type) {
        case ENABLE_DISABLE_DEVICE:
        case SET_DEVICE_BROKEN_UNBROKEN:
            cmd->args.device = get_uint16(&data);
            if (cmd->args.device >= NUM_DEVICES) {
                return true;
            }
            break;
        case UPDATE_DISPLAY:
            break; // the bitmap is used straight from cmd->data
        case SLEEP_TRANSMIT:
            cmd->args.sleep_time = get_uint32(&data);
            break;
        case DISABLE_TRANSMIT:
            break;
        case REBOOT:
            cmd->args.reboot = get_uint8(&data);
            break;
        case TAKE_PHOTO:
            cmd->args.photo_timestamp = get_uint32(&data);
            break;
        case SEND_PHOTO:
            cmd->args.photo_count = (int32_t)get_uint32(&data);
            break;
        case SET_KEPLER_COEFF:
            for (size_t i = 0; i < UPLINK_KEPLER_COEFFICIENTS; i++) {
                cmd->args.kepler_coefficients[i] = get_float(&data);
            }
            break;
        case SET_UNIX_TIME:
            cmd->args.unix_time = get_uint32(&data);
            break;
        case SET_ADCS_OP_MODE:
            cmd->args.adcs_op_mode = get_uint16(&data);
            break;
        case ADCS_UPDATE_PARAMS:
            cmd->args.adcs_parameters.timestamp = get_uint32(&data);
            cmd->args.adcs_parameters.inclination = get_float(&data);
            cmd->args.adcs_parameters.raan = get_float(&data);
            cmd->args.adcs_parameters.eccentricity = get_float(&data);
            cmd->args.adcs_parameters.argument_of_perigee = get_float(&data);
            cmd->args.adcs_parameters.mean_anomaly = get_float(&data);
            cmd->args.adcs_parameters.mean_motion = get_float(&data);
            break;
        case SET_PWR_MODE:
            cmd->args.power_mode = get_uint16(&data);
            break;
        default:
            return true;
    }
    return false;
}

bool uplink_packet_parse(uplink_packet_t *packet, const uint8_t *buf, size_t len) {
    if (packet == NULL || buf == NULL) {
        return true;
    }

    uplink_reader_t r = {.buf = buf, .size = len};

    // ~~~ Preamble ~~~
    const uint8_t *callsign = consume(&r, UPLINK_CALLSIGN_LENGTH);
    packet->message_size = get_uint32(&r);
    packet->n_cmds = get_uint16(&r);
    packet->timestamp = get_uint32(&r);
    if (r.overflow) {
        return true; // too short to hold the preamble
    }
    memcpy(packet->callsign, callsign, UPLINK_CALLSIGN_LENGTH);

    if (memcmp(packet->callsign, UPLINK_CALLSIGN, UPLINK_CALLSIGN_LENGTH) != 0) {
        return true; // not from our ground station
    }
    if (packet->message_size > len - UPLINK_PREAMBLE_SIZE || packet->n_cmds > UPLINK_MAX_CMDS) {
        return true;
    }
    r.size = UPLINK_PREAMBLE_SIZE + packet->message_size; // bytes past the end given by message_size are ignored

    // ~~~ Commands ~~~
    for (uint16_t i = 0; i < packet->n_cmds; i++) {
        if (parse_command(&r, &packet->cmds[i])) {
            return true;
        }
    }

    if (r.offset != r.size) {
        return true; // bytes left over after the last command
    }
    return false;
}
