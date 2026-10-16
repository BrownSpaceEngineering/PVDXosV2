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

/**
 * example for how we might use this spp_packet api
 */
void example(void) {
    spp_packet_t packet = spp_packet_create_header_only(0, 0, 0, 0, 0, 0); // dummy values
    spp_packet_view_t view = spp_packet_view_from(&packet);
    spp_packet_view_clear_data(view); // zero the packet
}
