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

bool serialize_telemetry(size_t buf_size, uint8_t *buf, preamble_t *pre) {
    size_t buf_offset = 0;
    return serialize_preamble(pre, buf, buf_size, &buf_offset);
}