#include "telemetry.h"

#include <string.h>

#include "ccsds/spp.h"
#include "globals.h"

#define TEL_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

_Static_assert(TELEMETRY_CALLSIGN_LENGTH == SPP_SECONDARY_HEADER_CALLSIGN_SIZE, "downlink callsign must fill the SPP secondary header");
_Static_assert(sizeof(TELEMETRY_CALLSIGN) - 1 == TELEMETRY_CALLSIGN_LENGTH, "TELEMETRY_CALLSIGN must be TELEMETRY_CALLSIGN_LENGTH chars");

// Expected command data size, in bytes, for each command ID
static const uint32_t command_data_sizes[TEL_CMD_COUNT] = {
    [TEL_CMD_ENABLE_DISABLE_DEVICE] = 2,
    [TEL_CMD_SET_DEVICE_BROKEN] = 2,
    [TEL_CMD_UPDATE_DISPLAY] = TELEMETRY_DISPLAY_BITMAP_SIZE,
    [TEL_CMD_SLEEP] = 4,
    [TEL_CMD_REBOOT] = 1,
    [TEL_CMD_TAKE_PICTURE] = 4,
    [TEL_CMD_SEND_PICTURES] = 4,
    [TEL_CMD_SET_KEPLER_COEFFICIENTS] = 4 * TELEMETRY_KEPLER_COEFFICIENTS,
    [TEL_CMD_SET_UNIX_TIME] = 4,
    [TEL_CMD_SET_ADCS_OPMODE] = 2,
    [TEL_CMD_ADCS_UPDATE_PARAMETERS] = 4 + 4 * 6,
    [TEL_CMD_SET_POWER_MODE] = 2,
};

/* ---------- WRITING ---------- */

// Writes values into a buffer, big-endian. Once a write doesn't fit, nothing more is written and overflow stays set,
// so a whole sequence of writes can be checked once at the end
typedef struct {
    uint8_t *buf;
    size_t size;
    size_t offset;
    bool overflow;
} tel_writer_t;

/**
 * Reserves n bytes in w
 *
 * \return pointer to the reserved bytes, or NULL if they don't fit
 */
static uint8_t *reserve(tel_writer_t *w, size_t n) {
    if (w->overflow || w->size - w->offset < n) {
        w->overflow = true;
        return NULL;
    }
    uint8_t *p = &w->buf[w->offset];
    w->offset += n;
    return p;
}

static void put_uint8(tel_writer_t *w, uint8_t value) {
    uint8_t *p = reserve(w, 1);
    if (p) {
        p[0] = value;
    }
}

static void put_uint16(tel_writer_t *w, uint16_t value) {
    uint8_t *p = reserve(w, 2);
    if (p) {
        p[0] = (value >> 8) & 0xFF;
        p[1] = value & 0xFF;
    }
}

static void put_uint32(tel_writer_t *w, uint32_t value) {
    uint8_t *p = reserve(w, 4);
    if (p) {
        p[0] = (value >> 24) & 0xFF;
        p[1] = (value >> 16) & 0xFF;
        p[2] = (value >> 8) & 0xFF;
        p[3] = value & 0xFF;
    }
}

static void put_float(tel_writer_t *w, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_uint32(w, bits);
}

static void put_bytes(tel_writer_t *w, const void *src, size_t n) {
    uint8_t *p = reserve(w, n);
    if (p) {
        memcpy(p, src, n);
    }
}

static void put_floats(tel_writer_t *w, const float *values, size_t n) {
    for (size_t i = 0; i < n; i++) {
        put_float(w, values[i]);
    }
}

static void put_int32s(tel_writer_t *w, const int32_t *values, size_t n) {
    for (size_t i = 0; i < n; i++) {
        put_uint32(w, (uint32_t)values[i]);
    }
}

static void put_uint32s(tel_writer_t *w, const uint32_t *values, size_t n) {
    for (size_t i = 0; i < n; i++) {
        put_uint32(w, values[i]);
    }
}

// Booleans are sent as TELEMETRY_TRUE or TELEMETRY_FALSE rather than whatever nonzero value they hold
static void put_bool(tel_writer_t *w, bool value) {
    put_uint8(w, value ? TELEMETRY_TRUE : TELEMETRY_FALSE);
}

static void put_status(tel_writer_t *w, tel_device_status_t status) {
    put_uint8(w, (uint8_t)status);
}

/* ---------- READING ---------- */

// Reads values out of a buffer, big-endian. Once a read doesn't fit, every read returns 0 and overflow stays set,
// so a whole sequence of reads can be checked once at the end
typedef struct {
    const uint8_t *buf;
    size_t size;
    size_t offset;
    bool overflow;
} tel_reader_t;

/**
 * Consumes n bytes from r
 *
 * \return pointer to the consumed bytes, or NULL if there aren't n left
 */
static const uint8_t *consume(tel_reader_t *r, size_t n) {
    if (r->overflow || r->size - r->offset < n) {
        r->overflow = true;
        return NULL;
    }
    const uint8_t *p = &r->buf[r->offset];
    r->offset += n;
    return p;
}

static uint8_t get_uint8(tel_reader_t *r) {
    const uint8_t *p = consume(r, 1);
    return p ? p[0] : 0;
}

static uint16_t get_uint16(tel_reader_t *r) {
    const uint8_t *p = consume(r, 2);
    return p ? (uint16_t)((p[0] << 8) | p[1]) : 0;
}

static uint32_t get_uint32(tel_reader_t *r) {
    const uint8_t *p = consume(r, 4);
    return p ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3] : 0;
}

static float get_float(tel_reader_t *r) {
    uint32_t bits = get_uint32(r);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* ---------- DOWNLINK ---------- */

/**
 * Writes the SPP primary and secondary headers for a downlink packet
 */
static void serialize_spp_headers(tel_writer_t *w, const tel_downlink_t *downlink) {
    // Primary header, every field packed most significant bit first
    // Reference: SPP Blue Book pg. 4-2
    uint16_t apid = TELEMETRY_DOWNLINK_APID & 0x7FF;
    uint16_t sequence_count = downlink->sequence_count & 0x3FFF;
    put_uint8(w, (SPP_VERSION_NUMBER << 5) | (SPP_PACKET_TYPE_REPORTING << 4) | (SPP_SECONDARY_HEADER_PRESENT << 3) | (apid >> 8));
    put_uint8(w, apid & 0xFF);
    put_uint16(w, (SPP_SEQ_FLAG_UNSEGMENTED_DATA << 14) | sequence_count);
    // The packet data length field holds the number of octets in the packet data field minus one
    put_uint16(w, TELEMETRY_DOWNLINK_SIZE - SPP_PRIMARY_HEADER_SIZE - 1);

    // Secondary header, in the order spp_packet_parse expects
    put_uint32(w, downlink->timestamp);
    put_uint32(w, downlink->boot_timestamp);
    put_bytes(w, TELEMETRY_CALLSIGN, TELEMETRY_CALLSIGN_LENGTH);
}

static void serialize_magnetometer(tel_writer_t *w, const tel_magnetometer_data_t *mag) {
    put_uint8(w, mag->revid_register);
    put_uint8(w, mag->bist_register);
    put_int32s(w, mag->raw_readings, TEL_ARRAY_LEN(mag->raw_readings));
    put_floats(w, mag->gain_adjusted_readings, TEL_ARRAY_LEN(mag->gain_adjusted_readings));
}

static void serialize_gyro(tel_writer_t *w, const tel_gyro_data_t *gyro) {
    put_int32s(w, gyro->raw_rate, TEL_ARRAY_LEN(gyro->raw_rate));
    put_floats(w, gyro->rate, TEL_ARRAY_LEN(gyro->rate));
    put_status(w, gyro->health);
}

static void serialize_eps(tel_writer_t *w, const tel_eps_data_t *eps) {
    put_floats(w, eps->charge_percent, TEL_ARRAY_LEN(eps->charge_percent));
    put_floats(w, eps->voltage, TEL_ARRAY_LEN(eps->voltage));
    put_floats(w, eps->current, TEL_ARRAY_LEN(eps->current));
    put_floats(w, eps->temperature, TEL_ARRAY_LEN(eps->temperature));
}

static void serialize_state_machine(tel_writer_t *w, const tel_state_machine_data_t *sm) {
    put_bytes(w, sm->state_history, sizeof(sm->state_history));
    put_floats(w, sm->transition_times, TEL_ARRAY_LEN(sm->transition_times));
    put_bytes(w, sm->input_variables, sizeof(sm->input_variables));
}

static void serialize_adcs(tel_writer_t *w, const tel_adcs_data_t *adcs) {
    put_floats(w, adcs->position, TEL_ARRAY_LEN(adcs->position));
    put_floats(w, adcs->attitude, TEL_ARRAY_LEN(adcs->attitude));
    put_floats(w, adcs->state, TEL_ARRAY_LEN(adcs->state));
    put_floats(w, adcs->covariance, TEL_ARRAY_LEN(adcs->covariance));
}

static void serialize_mram(tel_writer_t *w, const tel_mram_data_t *mram) {
    put_bytes(w, mram->status, sizeof(mram->status));
    put_uint32(w, mram->reflash_count);
    put_uint32s(w, mram->reflash_timestamps, TEL_ARRAY_LEN(mram->reflash_timestamps));
}

static void serialize_os_errors(tel_writer_t *w, const tel_os_error_data_t *os) {
    put_bytes(w, os->error_codes, sizeof(os->error_codes));
    put_uint32s(w, os->error_timestamps, TEL_ARRAY_LEN(os->error_timestamps));
}

bool telemetry_downlink_serialize(const tel_downlink_t *downlink, uint8_t *buf, size_t buf_size) {
    if (downlink == NULL || buf == NULL || buf_size < TELEMETRY_DOWNLINK_SIZE) {
        return true;
    }

    // Only ever write TELEMETRY_DOWNLINK_SIZE bytes, even if buf is bigger
    tel_writer_t w = {.buf = buf, .size = TELEMETRY_DOWNLINK_SIZE};

    serialize_spp_headers(&w, downlink);
    put_uint8(&w, downlink->last_bootloader);
    serialize_magnetometer(&w, &downlink->magnetometer);
    put_int32s(&w, downlink->magnetorquer.current, TEL_ARRAY_LEN(downlink->magnetorquer.current));
    put_floats(&w, downlink->photodiode.raw_readings, TEL_ARRAY_LEN(downlink->photodiode.raw_readings));
    serialize_gyro(&w, &downlink->gyro);
    put_status(&w, downlink->camera.health);
    put_bool(&w, downlink->camera.last_capture_succeeded);
    put_uint8(&w, downlink->camera.resolution);
    put_status(&w, downlink->display_health);
    put_status(&w, downlink->uhf_health);
    put_status(&w, downlink->sband.health);
    put_uint32(&w, downlink->sband.last_transmission_timestamp);
    serialize_eps(&w, &downlink->eps);
    serialize_state_machine(&w, &downlink->state_machine);
    serialize_adcs(&w, &downlink->adcs);
    for (size_t i = 0; i < TEL_ARRAY_LEN(downlink->command_timestamps); i++) {
        put_uint16(&w, downlink->command_timestamps[i]);
    }
    serialize_mram(&w, &downlink->mram);
    serialize_os_errors(&w, &downlink->os_errors);

    if (w.overflow) {
        return true; // the fields don't fit in TELEMETRY_DOWNLINK_SIZE
    }

    // Spare space at the end of the packet
    memset(&buf[w.offset], 0, TELEMETRY_DOWNLINK_SIZE - w.offset);
    return false;
}

/* ---------- UPLINK ---------- */

/**
 * Parses one command (ID, data size and data) from r into cmd
 *
 * \return true on failure (out of bytes, unknown ID, wrong data size, or invalid data), else false
 */
static bool parse_command(tel_reader_t *r, tel_command_t *cmd) {
    uint16_t id = get_uint16(r);
    cmd->data_size = get_uint32(r);
    if (r->overflow || id >= TEL_CMD_COUNT || cmd->data_size != command_data_sizes[id]) {
        return true;
    }
    cmd->id = (tel_command_id_t)id;

    cmd->data = consume(r, cmd->data_size);
    if (cmd->data == NULL) {
        return true; // the command's data runs past the end of the packet
    }

    // Decode the data, which we know is exactly the right size
    tel_reader_t data = {.buf = cmd->data, .size = cmd->data_size};
    switch (cmd->id) {
        case TEL_CMD_ENABLE_DISABLE_DEVICE:
        case TEL_CMD_SET_DEVICE_BROKEN:
            cmd->args.device = get_uint16(&data);
            if (cmd->args.device >= NUM_DEVICES) {
                return true;
            }
            break;
        case TEL_CMD_UPDATE_DISPLAY:
            break; // the bitmap is used straight from cmd->data
        case TEL_CMD_SLEEP:
            cmd->args.sleep_time = get_uint32(&data);
            break;
        case TEL_CMD_REBOOT:
            cmd->args.reboot = get_uint8(&data);
            break;
        case TEL_CMD_TAKE_PICTURE:
            cmd->args.picture_timestamp = get_uint32(&data);
            break;
        case TEL_CMD_SEND_PICTURES:
            cmd->args.picture_count = (int32_t)get_uint32(&data);
            break;
        case TEL_CMD_SET_KEPLER_COEFFICIENTS:
            for (size_t i = 0; i < TELEMETRY_KEPLER_COEFFICIENTS; i++) {
                cmd->args.kepler_coefficients[i] = get_float(&data);
            }
            break;
        case TEL_CMD_SET_UNIX_TIME:
            cmd->args.unix_time = get_uint32(&data);
            break;
        case TEL_CMD_SET_ADCS_OPMODE:
            cmd->args.adcs_opmode = get_uint16(&data);
            break;
        case TEL_CMD_ADCS_UPDATE_PARAMETERS:
            cmd->args.adcs_parameters.timestamp = get_uint32(&data);
            cmd->args.adcs_parameters.inclination = get_float(&data);
            cmd->args.adcs_parameters.raan = get_float(&data);
            cmd->args.adcs_parameters.eccentricity = get_float(&data);
            cmd->args.adcs_parameters.argument_of_perigee = get_float(&data);
            cmd->args.adcs_parameters.mean_anomaly = get_float(&data);
            cmd->args.adcs_parameters.mean_motion = get_float(&data);
            break;
        case TEL_CMD_SET_POWER_MODE:
            cmd->args.power_mode = get_uint16(&data);
            break;
        default:
            return true;
    }
    return false;
}

bool telemetry_uplink_parse(tel_uplink_t *uplink, uint8_t *buf, size_t len) {
    if (uplink == NULL || buf == NULL) {
        return true;
    }

    spp_packet_view_t view;
    if (spp_packet_parse(&view, buf, len > UINT32_MAX ? UINT32_MAX : (uint32_t)len)) {
        return true;
    }
    if (view.header.packet_type != SPP_PACKET_TYPE_REQUESTING) {
        return true; // not a telecommand
    }

    // Everything after the SPP headers, up to the end given by the packet data length field
    size_t packet_size = SPP_PRIMARY_HEADER_SIZE + (size_t)view.header.data_length + 1;
    const uint8_t *user_data = view.data;
    tel_reader_t r = {.buf = user_data, .size = packet_size - (size_t)(user_data - buf)};

    // ~~~ Preamble ~~~
    const uint8_t *callsign = consume(&r, TELEMETRY_CALLSIGN_LENGTH);
    uplink->message_size = get_uint32(&r);
    uplink->num_commands = get_uint16(&r);
    uplink->timestamp = get_uint32(&r);
    if (r.overflow) {
        return true; // too short to hold the preamble
    }
    memcpy(uplink->callsign, callsign, TELEMETRY_CALLSIGN_LENGTH);

    if (uplink->message_size != packet_size || uplink->num_commands > TELEMETRY_UPLINK_MAX_COMMANDS) {
        return true;
    }

    // ~~~ Commands ~~~
    for (uint16_t i = 0; i < uplink->num_commands; i++) {
        if (parse_command(&r, &uplink->commands[i])) {
            return true;
        }
    }

    if (r.offset != r.size) {
        return true; // bytes left over after the last command
    }
    return false;
}
