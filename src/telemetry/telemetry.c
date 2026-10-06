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
static bool serialize_preamble(preamble_t *preamble, uint8_t *buf, size_t buf_size, size_t *offset) {
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
static bool serialize_magnetometer(magnetometer_data_t *mag_data, uint8_t *buf, size_t buf_size, size_t *offset) {
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

bool serialize_telemetry(size_t buf_size, uint8_t *buf, preamble_t *pre, magnetometer_data_t *mag) {
    size_t buf_offset = 0;
    if (serialize_preamble(pre, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_magnetometer(mag, buf, buf_size, &buf_offset)) {
        return true;
    }
    return false;
}