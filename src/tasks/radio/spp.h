/**
 * src/task/rado/spp.h
 *
 * header file for the PVDX implementation of the CCSDS Space Pactket Protocol (SPP)
 *
 * Created: 20251026 SUN
 * Updated: 20251030 THU
 * Authors: Zach Mahan
 */

#ifndef RADIO_SPP_H
#define RADIO_SPP_H

#include <stddef.h>
#include <stdint.h>
// constants:
// version number, always 000, see SPP Blue Book 4.1.3.2.2 (pg. 4-2):
#define SPP_VERSION_NUMBER 0
// packet type, see SPP Blue Book 4.1.3.3.2.3 (pg. 4-3)
#define SPP_PACKET_TYPE_REPORTING 0  // telemetry
#define SPP_PACKET_TYPE_REQUESTING 1 // telecommands
// secondary header flag:
#define SPP_SECONDARY_HEADER_PRESENT 1
#define SPP_SECONDARY_HEADER_NOT_PRESENT 0
// Application Process Identifer (APID), see SPP Blue Book 4.1.3.3.4.4 (pg. 4-4)
#define SPP_APID_IDLE_PACKET 0b11111111111 // reserved value for idle packets
// sequence flags, see SPP Blue Book 4.1.3.4.2.2 (pg. 4-4 - 4-5)
#define SPP_SEQ_FLAG_CONTINUATION_OF_DATA 0b00
#define SPP_SEQ_FLAG_FIRST_SEGMENT_OF_DATA 0b01
#define SPP_SEQ_FLAG_LAST_SEGMENT_OF_DATA 0b10
#define SPP_SEQ_FLAG_UNSEGMENTED_DATA 0b11

/*
 * TODO:
 * decide on our standard packet size if we want to keep fixed-size buffers
 * within the packet struct itself. this should a value that can cleanly partition any data
 * that we'd want to transmit.
 *
 * We also don't really need a "standard size", since we can spp_packet_view_t's
 * instead which would let us be slightly more generic with how we build packets.
 */
#define SPP_STANDARD_PACKET_SIZE 512 // abitrarily chosen for now

/*
 * spp primary packet header, bitfield struct, see SPP Blue Book pg. 4-2
 */
typedef struct spp_primary_packet_header {
    // leading info (2 bytes):
    uint8_t version_number : 3;
    // packet identification
    uint8_t packet_type : 1;
    uint8_t secondary_header_flag : 1;
    uint16_t application_process_identifier : 11; // "APID", indicates source, destination, or type
    // packet sequence ctrl (2 bytes):
    uint8_t sequence_flags : 2;
    uint16_t sequence_count : 14; // gives numerical ordering for packets
    // packet data length (2 bytes):
    uint16_t data_length; // NOTE: this should be <number_of_bytes> - 1, see SPP Blue Book 4.1.3.5.3
} spp_primary_packet_header_t;

/*
 * Note: this is the optional secondary header where we entirely define its length and
 * contents. The bluebook states that there is an option for an ancillary data field for
 * "time, internal data field format, spacecraft position/attitude, etc."
 * See SPP Blue Book pg. 4-7
 * TODO: not included in our header, add in if we want additional, GSW currently is not using this either
 *
 */
typedef struct spp_secondary_packet_header {
    uint32_t time;
    // any more info to include?
} spp_secondary_packet_header_t;

/*
 * standard spp packet with an in-place data field
 */
typedef struct spp_packet {
    spp_primary_packet_header_t header;
    uint8_t data[SPP_STANDARD_PACKET_SIZE];
} spp_packet_t;

/*
 * standard spp packet with a view into a buffer as its data field
 */
typedef struct spp_packet_view {
    spp_primary_packet_header_t header;
    void *data;
    // ^ use header.data_length for bounds, but remember data_length = <number_of_bytes> - 1
} spp_packet_view_t;

/*
 * Presumably, these are the SPP functions that CFDP will need to wrap.
 * Here is the pseudocode for the SPP functions provided by CCSDS Blue Book (3.4.3.2.2 & 3.4.3.3.2):
 *
 * OCTET_STRING_request(Octet String, APID, Secondary Header Indicator, Packet Type, Packet Sequence Count / Packet Name);
 *
 * OCTET_STRING_indication(Octet String, APID, Secondary Header Indicator, Data Loss Indicator(optional));
 */

/**
 * ctor for building packets w/ in-place buffers, does NOT zero-initialize data buffer
 */
spp_packet_t spp_packet_create_header_only(uint16_t apid, uint8_t secondary_header_flag, uint8_t packet_type, uint8_t sequence_flags,
                                           uint16_t packet_seq_count, uint16_t data_length);

/**
 * ctor for building a packet w/ a zero-initialized data buffer
 */
spp_packet_t spp_packet_create_zero_init(uint16_t apid, uint8_t secondary_header_flag, uint8_t packet_type, uint8_t sequence_flags,
                                         uint16_t packet_seq_count_or_name, uint16_t data_length);

/**
 * ctor/initalizer for packet_view_t, useful if we want to store data externally and use packet_view_t's for everything
 */
void spp_packet_view_init(spp_packet_view_t *packet, void *data, uint16_t apid, uint8_t secondary_header_flag, uint8_t packet_type,
                          uint8_t sequence_flags, uint16_t packet_seq_count, uint16_t data_length);

/**
 * ctor for packet_view_t from a packet
 */
spp_packet_view_t spp_packet_view_from(spp_packet_t *packet);
/**
 * help to clear/zero a packet_views's data
 */
void spp_packet_view_clear_data(spp_packet_view_t view);
#endif // !RADIO_SPP_H
