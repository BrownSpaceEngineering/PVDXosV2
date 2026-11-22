#include "uplink_parse.h"

#include <string.h>

#include "globals.h"
#include "telemetry/telemetry.h"

_Static_assert(sizeof(UPLINK_CALLSIGN) - 1 == UPLINK_CALLSIGN_LENGTH, "UPLINK_CALLSIGN must be UPLINK_CALLSIGN_LENGTH chars");

// Expected command data size, in bytes, for each command type (the "Size" column of the uplink sheet)
static const uint32_t command_data_sizes[UPLINK_CMD_COUNT] = {
    [ENABLE_DISABLE_DEVICE] = 3,
    [UPDATE_DISPLAY] = UPLINK_DISPLAY_BITMAP_SIZE,
    [TAKE_IV_CURVE] = 4,
    [SEND_IV_CURVE] = 1,
    [TRANSMIT_ON_OFF] = 1,
    [REBOOT] = 1,
    [TAKE_PHOTO] = 4,
    [SEND_PHOTO] = 4,
    [SET_KEPLER_COEFF] = 4 * UPLINK_KEPLER_COEFFICIENTS,
    [SET_UNIX_TIME] = 4,
    [SET_ADCS_OP_MODE] = 2,
    [ADCS_UPDATE_PARAMS] = UPLINK_ADCS_PARAMETERS_SIZE,
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

static double get_double(uplink_reader_t *r) {
    const uint64_t high = get_uint32(r);
    const uint64_t bits = (high << 32) | get_uint32(r);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* ---------- PARSING ---------- */

/**
 * Parses ADCS_UPDATE_PARAMS data (see uplink_adcs_parameters_t) from r into params
 *
 * \return true if the flags byte has a reserved bit set, else false
 */
static bool parse_adcs_parameters(uplink_reader_t *r, uplink_adcs_parameters_t *params) {
    params->epoch_jd = get_double(r);
    params->inclination = get_float(r);
    params->raan = get_float(r);
    params->eccentricity = get_float(r);
    params->argument_of_perigee = get_float(r);
    params->mean_anomaly = get_float(r);
    params->mean_motion = get_float(r);
    params->bstar = get_float(r);
    for (size_t i = 0; i < 3; i++) {
        params->device_rotation[i] = (int8_t)get_uint8(r);
    }

    const uint8_t flags = get_uint8(r);
    if (flags & 0x07) {
        return true; // reserved bits set: the ground's layout doesn't match ours
    }
    for (size_t i = 0; i < 3; i++) {
        params->flip_magnetorquer_current[i] = (flags >> (7 - i)) & 1;
    }
    params->bdot_override = (flags >> 4) & 1;
    params->dipole_off = (flags >> 3) & 1;
    return false;
}

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
    if (cmd->data_size != command_data_sizes[cmd->type]) {
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
            cmd->args.device_state.device = get_uint16(&data);
            cmd->args.device_state.enabled = get_uint8(&data);
            if (cmd->args.device_state.device >= NUM_DEVICES || cmd->args.device_state.enabled > 1) {
                return true;
            }
            break;
        case UPDATE_DISPLAY:
            break; // the bitmap is used straight from cmd->data
        case TAKE_IV_CURVE:
            cmd->args.iv_sampling_resolution = get_uint32(&data);
            break;
        case SEND_IV_CURVE:
            break; // its byte has no meaning
        case TRANSMIT_ON_OFF:
            // TODO: the sheet doesn't say what this byte means (its data is N/A), or whether transmit should be turned
            // on/off with this command at all or only through PUSP notifications (see PUSP_NOTIF_DISABLE_TX)
            cmd->args.transmit = get_uint8(&data);
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
            if (cmd->args.adcs_op_mode >= TEL_ADCS_OPMODE_COUNT) {
                return true;
            }
            break;
        case ADCS_UPDATE_PARAMS:
            if (parse_adcs_parameters(&data, &cmd->args.adcs_parameters)) {
                return true;
            }
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
