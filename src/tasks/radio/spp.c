/**
 * src/task/rado/spp.c
 *
 * src file for the PVDX implementation of the CCSDS Space Pactket Protocol (SPP)
 *
 * Created: 20251026 SUN
 * Authors: Zach Mahan
 */

#include "spp.h"

/*
 * ctor/initalizer for packet_view_t
 */
void spp_packet_view_create(spp_packet_view_t *packet, void *data, uint16_t apid, uint8_t secondary_header_flag, uint8_t packet_type,
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
