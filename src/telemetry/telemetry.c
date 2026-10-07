#include "telemetry.h"

#include <string.h>

// static uint8_t telemetry_buf[TELEMETRY_PACKET_SIZE];

/**
 * Writes a uint32_t into buf at *offset, big-endian (most significant byte first)
 *
 * \param buf - buffer to write into
 * \param buf_len - total size of buf, in bytes
 * \param offset - position to write at; advanced by 4 on success
 * \param value - number to write
 *
 * \return true on failure (not enough room in buf), else false
 */
static bool put_uint32(uint8_t *buf, size_t buf_len, size_t *offset, uint32_t value) {
    // make sure all 4 bytes fit before writing anything
    if (buf_len < 4 || *offset > buf_len - 4) {
        return true;
    }

    buf[*offset + 0] = (value >> 24) & 0xFF;
    buf[*offset + 1] = (value >> 16) & 0xFF;
    buf[*offset + 2] = (value >> 8) & 0xFF;
    buf[*offset + 3] = value & 0xFF;

    *offset += 4;
    return false;
}

/**
 * Writes a uint16_t into buf at *offset, big-endian (most significant byte first)
 *
 * \param buf - buffer to write into
 * \param buf_len - total size of buf, in bytes
 * \param offset - position to write at; advanced by 2 on success
 * \param value - number to write
 *
 * \return true on failure (not enough room in buf), else false
 */
static bool put_uint16(uint8_t *buf, size_t buf_len, size_t *offset, uint16_t value) {
    // make sure both bytes fit before writing anything
    if (buf_len < 2 || *offset > buf_len - 2) {
        return true;
    }

    buf[*offset + 0] = (value >> 8) & 0xFF;
    buf[*offset + 1] = value & 0xFF;

    *offset += 2;
    return false;
}

/**
 * Writes a uint8_t into buf at *offset
 *
 * \param buf - buffer to write into
 * \param buf_len - total size of buf, in bytes
 * \param offset - position to write at; advanced by 1 on success
 * \param value - number to write
 *
 * \return true on failure (not enough room in buf), else false
 */
static bool put_uint8(uint8_t *buf, size_t buf_len, size_t *offset, uint8_t value) {
    // make sure the byte fits before writing it
    if (*offset >= buf_len) {
        return true;
    }

    buf[*offset] = value;

    *offset += 1;
    return false;
}

/**
 * Function for serializing the telemetry preamble
 */
static bool serialize_preamble(tel_preamble_t *preamble, uint8_t *buf, size_t buf_size, size_t *offset) {
    if (buf_size < TELEMETRY_CALLSIGN_LENGTH || *offset > buf_size - TELEMETRY_CALLSIGN_LENGTH) {
        return true;
    }
    memcpy((buf + *offset), preamble->callsign, TELEMETRY_CALLSIGN_LENGTH);
    *offset += TELEMETRY_CALLSIGN_LENGTH;
    if (put_uint32(buf, buf_size, offset, preamble->state)) {
        return true;
    }
    if (put_uint32(buf, buf_size, offset, preamble->timestamp)) {
        return true;
    }
    if (put_uint32(buf, buf_size, offset, preamble->message_size)) {
        return true;
    }
    return false;
}

/**
 * Function for serializing the magnetometer data
 */
static bool serialize_magnetometer(tel_magnetometer_data_t *mag_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    if (put_uint8(buf, buf_size, offset, mag_data->revid_register)) {
        return true;
    }
    if (put_uint8(buf, buf_size, offset, mag_data->bist_register)) {
        return true;
    }
    if (put_uint32(buf, buf_size, offset, mag_data->timestamp)) {
        return true;
    }
    // Raw readings
    for (size_t i = 0; i < 3; i++) {
        if (put_uint32(buf, buf_size, offset, (uint32_t)mag_data->raw_readings[i])) {
            return true;
        }
    }
    // Gain adjusted readings
    for (size_t i = 0; i < 3; i++) {
        uint32_t bits;
        memcpy(&bits, &mag_data->gain_adjusted_readings[i], sizeof(bits));
        if (put_uint32(buf, buf_size, offset, bits)) {
            return true;
        }
    }
    return false;
}

/**
 * Function for serializing the magnetorquer data
 */
static bool serialize_magnetorquer(tel_magnetorquer_data_t *mag_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    for (size_t i = 0; i < 3; i++) {
        if (put_uint32(buf, buf_size, offset, (uint32_t)mag_data->current[i])) {
            return true;
        }
    }
    return false;
}

/**
 * Function for serializing the photodiode data
 */
static bool serialize_photodiode(tel_photodiode_data_t *photo_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    for (size_t i = 0; i < 22; i++) {
        uint32_t bits;
        memcpy(&bits, &photo_data->raw_readings[i], sizeof(bits));
        if (put_uint32(buf, buf_size, offset, bits)) {
            return true;
        }
    }
    return false;
}

/**
 * Function for serializing the gyro data
 */
static bool serialize_gyro(tel_gyro_data_t *gyro_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    // TODO: tel_gyro_data_t has no fields yet, so there is nothing to write
    return false;
}

/**
 * Function for serializing the peripheral health flags
 */
static bool serialize_peripheral_status(tel_peripheral_status_t *status, uint8_t *buf, size_t buf_size, size_t *offset) {
    // Sends an entire byte full of 1s or 0s based on whether healthy or not - prevents bit flips
    if (put_uint8(buf, buf_size, offset, status->display ? TELEMETRY_TRUE : TELEMETRY_FALSE)) {
        return true;
    }
    if (put_uint8(buf, buf_size, offset, status->camera ? TELEMETRY_TRUE : TELEMETRY_FALSE)) {
        return true;
    }
    if (put_uint8(buf, buf_size, offset, status->uhf_radio ? TELEMETRY_TRUE : TELEMETRY_FALSE)) {
        return true;
    }
    if (put_uint8(buf, buf_size, offset, status->sband_radio ? TELEMETRY_TRUE : TELEMETRY_FALSE)) {
        return true;
    }
    return false;
}

/**
 * Function for serializing the EPS board data
 */
static bool serialize_eps(tel_eps_data_t *eps_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    uint32_t bits;
    memcpy(&bits, &eps_data->charge, sizeof(bits));
    if (put_uint32(buf, buf_size, offset, bits)) {
        return true;
    }
    return false;
}

/**
 * Function for serializing the state machine data
 */
static bool serialize_state_machine(tel_state_machine_data_t *sm_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    if (put_uint16(buf, buf_size, offset, sm_data->state)) {
        return true;
    }
    return false;
}

/**
 * Function for serializing the ADCS data
 */
static bool serialize_adcs(tel_adcs_data_t *adcs_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    if (put_uint16(buf, buf_size, offset, adcs_data->attitude)) {
        return true;
    }
    if (put_uint8(buf, buf_size, offset, adcs_data->bdot_status)) {
        return true;
    }
    return false;
}

/**
 * Function that serializes all of the telemetry data into the buffer
 */
bool serialize_telemetry(size_t buf_size, uint8_t *buf, telemetry_data_t *data) {
    size_t buf_offset = 0;
    if (serialize_preamble(&data->preamble, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_magnetometer(&data->magnetometer, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_magnetorquer(&data->magnetorquer, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_photodiode(&data->photodiode, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_gyro(&data->gyro, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_peripheral_status(&data->peripheral_status, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_eps(&data->eps, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_state_machine(&data->state_machine, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_adcs(&data->adcs, buf, buf_size, &buf_offset)) {
        return true;
    }
    return false;
}