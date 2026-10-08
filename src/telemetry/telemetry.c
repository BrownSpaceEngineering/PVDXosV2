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
 * Function for serializing the SPP primary header
 */
static bool serialize_spp_primary(spp_primary_packet_header_t *primary, uint8_t *buf, size_t buf_size, size_t *offset) {
    // version (3 bits) | packet type (1 bit) | secondary header flag (1 bit) | APID (11 bits)
    uint16_t packet_id = (primary->version_number << 13) | (primary->packet_type << 12) | (primary->secondary_header_flag << 11) |
                         (primary->application_process_id & 0x7FF);
    if (put_uint16(buf, buf_size, offset, packet_id)) {
        return true;
    }
    // sequence flags (2 bits) | sequence count (14 bits)
    uint16_t sequence_control = (primary->sequence_flags << 14) | (primary->sequence_count & 0x3FFF);
    if (put_uint16(buf, buf_size, offset, sequence_control)) {
        return true;
    }
    if (put_uint16(buf, buf_size, offset, primary->data_length)) {
        return true;
    }
    return false;
}

/**
 * Function for serializing the SPP secondary header
 */
static bool serialize_spp_secondary(spp_secondary_packet_header_t *secondary, uint8_t *buf, size_t buf_size, size_t *offset) {
    // Same order spp_packet_parse reads it in: timestamp, boot timestamp, callsign
    if (put_uint32(buf, buf_size, offset, secondary->timestamp)) {
        return true;
    }
    if (put_uint32(buf, buf_size, offset, secondary->boot_timestamp)) {
        return true;
    }
    if (buf_size < SPP_SECONDARY_HEADER_CALLSIGN_SIZE || *offset > buf_size - SPP_SECONDARY_HEADER_CALLSIGN_SIZE) {
        return true;
    }
    memcpy((buf + *offset), secondary->callsign, SPP_SECONDARY_HEADER_CALLSIGN_SIZE);
    *offset += SPP_SECONDARY_HEADER_CALLSIGN_SIZE;
    return false;
}

/**
 * Function for serializing the boot data
 */
static bool serialize_boot(tel_boot_data_t *boot_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    if (put_uint8(buf, buf_size, offset, boot_data->last_bootloader)) {
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
    if (put_uint16(buf, buf_size, offset, gyro_data->status)) {
        return true;
    }
    // Raw readings
    for (size_t i = 0; i < 3; i++) {
        if (put_uint32(buf, buf_size, offset, (uint32_t)gyro_data->raw_readings[i])) {
            return true;
        }
    }
    if (put_uint8(buf, buf_size, offset, gyro_data->health_status)) {
        return true;
    }
    return false;
}

/**
 * Function for serializing the camera data
 */
static bool serialize_camera(tel_camera_data_t *camera_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    if (put_uint8(buf, buf_size, offset, camera_data->health_status)) {
        return true;
    }
    // Sends an entire byte full of 1s or 0s based on whether the capture succeeded or not - prevents bit flips
    if (put_uint8(buf, buf_size, offset, camera_data->recent_capture_status ? TELEMETRY_TRUE : TELEMETRY_FALSE)) {
        return true;
    }
    if (put_uint8(buf, buf_size, offset, camera_data->resolution)) {
        return true;
    }
    return false;
}

/**
 * Function for serializing the display data
 */
static bool serialize_display(tel_display_data_t *display_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    if (put_uint8(buf, buf_size, offset, display_data->health_status)) {
        return true;
    }
    return false;
}

/**
 * Function for serializing the UHF radio data
 */
static bool serialize_uhf(tel_uhf_data_t *uhf_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    if (put_uint8(buf, buf_size, offset, uhf_data->health_status)) {
        return true;
    }
    return false;
}

/**
 * Function for serializing the S-Band radio data
 */
static bool serialize_sband(tel_sband_data_t *sband_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    if (put_uint8(buf, buf_size, offset, sband_data->health_status)) {
        return true;
    }
    if (put_uint32(buf, buf_size, offset, sband_data->last_transmission_timestamp)) {
        return true;
    }
    return false;
}

/**
 * Function for serializing the EPS board data
 */
static bool serialize_eps(tel_eps_data_t *eps_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    // Charge
    for (size_t i = 0; i < 4; i++) {
        uint32_t bits;
        memcpy(&bits, &eps_data->charge[i], sizeof(bits));
        if (put_uint32(buf, buf_size, offset, bits)) {
            return true;
        }
    }
    // Voltage
    for (size_t i = 0; i < 4; i++) {
        uint32_t bits;
        memcpy(&bits, &eps_data->voltage[i], sizeof(bits));
        if (put_uint32(buf, buf_size, offset, bits)) {
            return true;
        }
    }
    // Current
    for (size_t i = 0; i < 4; i++) {
        uint32_t bits;
        memcpy(&bits, &eps_data->current[i], sizeof(bits));
        if (put_uint32(buf, buf_size, offset, bits)) {
            return true;
        }
    }
    // Temperature
    for (size_t i = 0; i < 4; i++) {
        uint32_t bits;
        memcpy(&bits, &eps_data->temperature[i], sizeof(bits));
        if (put_uint32(buf, buf_size, offset, bits)) {
            return true;
        }
    }
    return false;
}

/**
 * Function for serializing the state machine data
 */
static bool serialize_state_machine(tel_state_machine_data_t *sm_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    // State history
    for (size_t i = 0; i < 8; i++) {
        if (put_uint8(buf, buf_size, offset, sm_data->state_history[i])) {
            return true;
        }
    }
    // Transition times
    for (size_t i = 0; i < 8; i++) {
        uint32_t bits;
        memcpy(&bits, &sm_data->transition_times[i], sizeof(bits));
        if (put_uint32(buf, buf_size, offset, bits)) {
            return true;
        }
    }
    // Input variables
    for (size_t i = 0; i < 3; i++) {
        if (put_uint8(buf, buf_size, offset, sm_data->input_variables[i])) {
            return true;
        }
    }
    return false;
}

/**
 * Function for serializing the ADCS data
 */
static bool serialize_adcs(tel_adcs_data_t *adcs_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    // Position
    for (size_t i = 0; i < 3; i++) {
        uint32_t bits;
        memcpy(&bits, &adcs_data->position[i], sizeof(bits));
        if (put_uint32(buf, buf_size, offset, bits)) {
            return true;
        }
    }
    // Attitude
    for (size_t i = 0; i < 3; i++) {
        uint32_t bits;
        memcpy(&bits, &adcs_data->attitude[i], sizeof(bits));
        if (put_uint32(buf, buf_size, offset, bits)) {
            return true;
        }
    }
    // Estimated state
    for (size_t i = 0; i < 7; i++) {
        uint32_t bits;
        memcpy(&bits, &adcs_data->estimated_state[i], sizeof(bits));
        if (put_uint32(buf, buf_size, offset, bits)) {
            return true;
        }
    }
    // Covariance matrix
    for (size_t i = 0; i < 4; i++) {
        uint32_t bits;
        memcpy(&bits, &adcs_data->covariance_matrix[i], sizeof(bits));
        if (put_uint32(buf, buf_size, offset, bits)) {
            return true;
        }
    }
    return false;
}

/**
 * Function for serializing the received commands data
 */
static bool serialize_commands(tel_commands_data_t *commands_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    for (size_t i = 0; i < 16; i++) {
        if (put_uint16(buf, buf_size, offset, commands_data->received_timestamps[i])) {
            return true;
        }
    }
    return false;
}

/**
 * Function for serializing the MRAM and redundancy data
 */
static bool serialize_mram(tel_mram_data_t *mram_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    // MRAM status
    for (size_t i = 0; i < 3; i++) {
        if (put_uint8(buf, buf_size, offset, mram_data->mram_status[i])) {
            return true;
        }
    }
    if (put_uint32(buf, buf_size, offset, mram_data->total_reflashes)) {
        return true;
    }
    // Reflash timestamps
    for (size_t i = 0; i < 8; i++) {
        if (put_uint32(buf, buf_size, offset, mram_data->reflash_timestamps[i])) {
            return true;
        }
    }
    return false;
}

/**
 * Function for serializing the OS errors data
 */
static bool serialize_os_errors(tel_os_errors_data_t *errors_data, uint8_t *buf, size_t buf_size, size_t *offset) {
    // Error codes
    for (size_t i = 0; i < 8; i++) {
        if (put_uint8(buf, buf_size, offset, errors_data->error_codes[i])) {
            return true;
        }
    }
    // Error timestamps
    for (size_t i = 0; i < 8; i++) {
        if (put_uint32(buf, buf_size, offset, errors_data->error_timestamps[i])) {
            return true;
        }
    }
    return false;
}

/**
 * Function for writing the reserved space at the end of the packet
 */
static bool serialize_reserved(uint8_t *buf, size_t buf_size, size_t *offset) {
    // Zeros, so every packet is TELEMETRY_PACKET_SIZE bytes and new fields can be added without changing the size
    for (size_t i = 0; i < TELEMETRY_RESERVED_SIZE; i++) {
        if (put_uint8(buf, buf_size, offset, 0)) {
            return true;
        }
    }
    return false;
}

/**
 * Function that serializes all of the telemetry data into the buffer
 */
bool serialize_telemetry(size_t buf_size, uint8_t *buf, telemetry_data_t *data) {
    size_t buf_offset = 0;
    if (serialize_spp_primary(&data->spp_primary, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_spp_secondary(&data->spp_secondary, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_boot(&data->boot, buf, buf_size, &buf_offset)) {
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
    if (serialize_camera(&data->camera, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_display(&data->display, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_uhf(&data->uhf, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_sband(&data->sband, buf, buf_size, &buf_offset)) {
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
    if (serialize_commands(&data->commands, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_mram(&data->mram, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_os_errors(&data->os_errors, buf, buf_size, &buf_offset)) {
        return true;
    }
    if (serialize_reserved(buf, buf_size, &buf_offset)) {
        return true;
    }
    return false;
}
