/**
 * src/ccsds/spp.c
 *
 * src file for the PVDX implementation of the CCSDS Space Pactket Protocol (SPP)
 *
 * Created: 20251026 SUN
 * Updated: 20261007 WED
 * Authors: Zach Mahan, Ilan Goldfein
 */

#include "spp.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "logging.h"
#include "utils_assert.h"

void spp_packet_view_init(spp_packet_view_t *packet, void *data, uint16_t apid, uint8_t secondary_header_flag, uint8_t packet_type,
                          uint8_t sequence_flags, uint16_t packet_seq_count_or_name, uint16_t data_length) {
    packet->data = data;
    packet->header.version_number = SPP_VERSION_NUMBER;
    packet->header.packet_type = packet_type;
    packet->header.application_process_id = apid;
    packet->header.secondary_header_flag = secondary_header_flag;
    packet->header.sequence_flags = sequence_flags;
    packet->header.sequence_count = packet_seq_count_or_name;
    packet->header.data_length = data_length;
    packet->secondary_header = (spp_secondary_packet_header_t){0};
}

spp_packet_t spp_packet_create_header_only(uint16_t apid, uint8_t secondary_header_flag, uint8_t packet_type, uint8_t sequence_flags,
                                           uint16_t packet_seq_count_or_name, uint16_t data_length) {
    spp_packet_t packet = {.header.version_number = SPP_VERSION_NUMBER,

                           .header.packet_type = packet_type,
                           .header.application_process_id = apid,
                           .header.secondary_header_flag = secondary_header_flag,
                           .header.sequence_flags = sequence_flags,
                           .header.sequence_count = packet_seq_count_or_name,
                           .header.data_length = data_length};
    return packet;
}

spp_packet_t spp_packet_create_zero_init(uint16_t apid, uint8_t secondary_header_flag, uint8_t packet_type, uint8_t sequence_flags,
                                         uint16_t packet_seq_count_or_name, uint16_t data_length) {
    spp_packet_t packet = {.header.version_number = SPP_VERSION_NUMBER,

                           .header.packet_type = packet_type,
                           .header.application_process_id = apid,
                           .header.secondary_header_flag = secondary_header_flag,
                           .header.sequence_flags = sequence_flags,
                           .header.sequence_count = packet_seq_count_or_name,
                           .header.data_length = data_length};
    // zero data
    for (size_t i = 0; i < SPP_STANDARD_PACKET_SIZE; i++) {
        ((uint8_t *)packet.data)[i] = 0;
    }
    return packet;
}

spp_packet_view_t spp_packet_view_from(spp_packet_t *packet) {
    spp_packet_view_t packet_view = {.header = packet->header, .data = packet->data};
    return packet_view;
}

/**
 * help to clear/zero a packet_views's data
 */
void spp_packet_view_clear_data(spp_packet_view_t view) {
    for (size_t i = 0; i < SPP_STANDARD_PACKET_SIZE; i++) {
        ((uint8_t *)view.data)[i] = 0;
    }
}

bool spp_packet_parse(spp_packet_view_t *view, uint8_t *data, uint32_t len) {
    if (view == NULL || data == NULL) {
        return true;
    }

    if (len < SPP_PRIMARY_HEADER_SIZE) {
        return true; // too short to hold the primary header
    }

    // ~~~ Packet Primary Header ~~~
    // Every field is packed most significant bit first
    spp_primary_packet_header_t *ph = &view->header;
    *ph = (spp_primary_packet_header_t){0};

    ph->version_number = data[0] >> 5;
    if (ph->version_number != SPP_VERSION_NUMBER) {
        return true; // not a space packet
    }

    ph->packet_type = (data[0] >> 4) & 0x01;
    ph->secondary_header_flag = (data[0] >> 3) & 0x01;
    ph->application_process_id = ((data[0] & 0x07) << 8) | data[1];
    ph->sequence_flags = data[2] >> 6;
    ph->sequence_count = ((data[2] & 0x3F) << 8) | data[3];
    ph->data_length = (data[4] << 8) | data[5];

    // The packet data length field holds the number of octets in the packet data field minus one
    // Reference: SPP Blue Book 4.1.3.5.3
    uint32_t packet_len = SPP_PRIMARY_HEADER_SIZE + (uint32_t)ph->data_length + 1;
    if (packet_len > len) {
        return true; // the packet claims to be longer than the data we were given
    }

    // ~~~ Packet Secondary Header ~~~
    view->secondary_header = (spp_secondary_packet_header_t){0};
    uint32_t header_len = SPP_PRIMARY_HEADER_SIZE;

    if (ph->secondary_header_flag) {
        header_len += SPP_SECONDARY_HEADER_SIZE;
        if (header_len > packet_len) {
            return true; // too short to hold the secondary header it says it has
        }

        // Each integer most significant octet first
        const uint8_t *sh = &data[SPP_PRIMARY_HEADER_SIZE];
        view->secondary_header.timestamp = ((uint32_t)sh[0] << 24) | ((uint32_t)sh[1] << 16) | ((uint32_t)sh[2] << 8) | sh[3];
        view->secondary_header.boot_timestamp = ((uint32_t)sh[4] << 24) | ((uint32_t)sh[5] << 16) | ((uint32_t)sh[6] << 8) | sh[7];
        memcpy(view->secondary_header.callsign, &sh[8], SPP_SECONDARY_HEADER_CALLSIGN_SIZE);
    }

    // ~~~ User Data Field ~~~
    view->data = &data[header_len];

    return false;
}

// TODO: have telemetry's serialize_spp_headers use this instead of packing the headers itself
bool spp_packet_serialize(const spp_primary_packet_header_t *primary, const spp_secondary_packet_header_t *secondary, const uint8_t *data,
                          size_t data_len, uint8_t *buf, size_t buf_size, size_t *packet_len) {
    if (primary == NULL || buf == NULL || packet_len == NULL || (data == NULL && data_len > 0)) {
        return true;
    }

    bool has_secondary = primary->secondary_header_flag;
    if (has_secondary && secondary == NULL) {
        return true;
    }

    // The packet data field is everything after the primary header: the secondary header (if any) and the data
    size_t data_field_len = (has_secondary ? SPP_SECONDARY_HEADER_SIZE : 0) + data_len;

    // The packet data length field holds data_field_len - 1, so the data field must be 1 to 65536 octets
    // Reference: SPP Blue Book 4.1.3.5.3
    if (data_field_len == 0 || data_field_len > 0x10000) {
        return true;
    }

    size_t total_len = SPP_PRIMARY_HEADER_SIZE + data_field_len;
    if (total_len > buf_size) {
        return true; // not enough room for the packet
    }

    // ~~~ Packet Primary Header ~~~
    // The inverse of the unpacking in spp_packet_parse: every field is most significant bit first
    uint16_t data_length = (uint16_t)(data_field_len - 1);
    buf[0] = (primary->version_number << 5) | (primary->packet_type << 4) | (has_secondary << 3) |
        ((primary->application_process_id >> 8) & 0x07);
    buf[1] = primary->application_process_id & 0xFF;
    buf[2] = (primary->sequence_flags << 6) | ((primary->sequence_count >> 8) & 0x3F);
    buf[3] = primary->sequence_count & 0xFF;
    buf[4] = data_length >> 8;
    buf[5] = data_length & 0xFF;

    size_t offset = SPP_PRIMARY_HEADER_SIZE;

    // ~~~ Packet Secondary Header ~~~
    // Same order spp_packet_parse reads it in, each integer most significant octet first
    if (has_secondary) {
        uint8_t *sh = &buf[offset];
        sh[0] = secondary->timestamp >> 24;
        sh[1] = (secondary->timestamp >> 16) & 0xFF;
        sh[2] = (secondary->timestamp >> 8) & 0xFF;
        sh[3] = secondary->timestamp & 0xFF;
        sh[4] = secondary->boot_timestamp >> 24;
        sh[5] = (secondary->boot_timestamp >> 16) & 0xFF;
        sh[6] = (secondary->boot_timestamp >> 8) & 0xFF;
        sh[7] = secondary->boot_timestamp & 0xFF;
        memcpy(&sh[8], secondary->callsign, SPP_SECONDARY_HEADER_CALLSIGN_SIZE);
        offset += SPP_SECONDARY_HEADER_SIZE;
    }

    // ~~~ User Data Field ~~~
    if (data_len > 0) {
        memcpy(&buf[offset], data, data_len);
    }

    *packet_len = total_len;
    return false;
}

/**
 * example for how we might use this spp_packet api
 */
void example(void) {
    spp_packet_t packet = spp_packet_create_header_only(0, 0, 0, 0, 0, 0); // dummy values
    spp_packet_view_t view = spp_packet_view_from(&packet);
    spp_packet_view_clear_data(view); // zero the packet
}
