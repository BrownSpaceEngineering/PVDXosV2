/**
 * src/task/rado/spp.c
 *
 * src file for the PVDX implementation of the CCSDS Space Pactket Protocol (SPP)
 *
 * Created: 20251026 SUN
 * Authors: Zach Mahan
 */

#include "spp.h"

#include <stddef.h>
#include <stdint.h>

void spp_packet_view_init(spp_packet_view_t *packet, void *data, uint16_t apid, uint8_t secondary_header_flag, uint8_t packet_type,
                          uint8_t sequence_flags, uint16_t packet_seq_count_or_name, uint16_t data_length) {
    packet->data = data;
    packet->header.version_number = SPP_VERSION_NUMBER;
    packet->header.packet_type = packet_type;
    packet->header.application_process_identifier = apid;
    packet->header.secondary_header_flag = secondary_header_flag;
    packet->header.sequence_flags = sequence_flags;
    packet->header.sequence_count = packet_seq_count_or_name;
    packet->header.data_length = data_length;
}

spp_packet_t spp_packet_create_header_only(uint16_t apid, uint8_t secondary_header_flag, uint8_t packet_type, uint8_t sequence_flags,
                                           uint16_t packet_seq_count_or_name, uint16_t data_length) {
    spp_packet_t packet = {.header.version_number = SPP_VERSION_NUMBER,

                           .header.packet_type = packet_type,
                           .header.application_process_identifier = apid,
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
                           .header.application_process_identifier = apid,
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

/**
 * example for how we might use this spp_packet api
 */
void example(void) {
    spp_packet_t packet = spp_packet_create_header_only(0, 0, 0, 0, 0, 0); // dummy values
    spp_packet_view_t view = spp_packet_view_from(&packet);
    spp_packet_view_clear_data(view); // zero the packet
}
