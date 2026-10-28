#include "telemetry.h"

#include <string.h>

#define TEL_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

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

/* ---------- DOWNLINK ---------- */

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

void telemetry_collect(tel_downlink_t *downlink) {
    // TODO: gather telemetry from each subsystem
    memset(downlink, 0, sizeof(*downlink));
}

bool telemetry_downlink_serialize(const tel_downlink_t *downlink, uint8_t *buf, size_t buf_size) {
    if (downlink == NULL || buf == NULL || buf_size < TELEMETRY_DOWNLINK_SIZE) {
        return true;
    }

    // Only ever write TELEMETRY_DOWNLINK_SIZE bytes, even if buf is bigger
    tel_writer_t w = {.buf = buf, .size = TELEMETRY_DOWNLINK_SIZE};

    // No SPP headers here: the file is sent with CFDP, which wraps each of its PDUs in an SPP packet itself
    put_uint32(&w, downlink->timestamp);
    put_uint32(&w, downlink->boot_timestamp);
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
