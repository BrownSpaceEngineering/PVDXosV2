/**
 * src/ccsds/spp.h
 *
 * header file for the PVDX implementation of the CCSDS Space Pactket Protocol (SPP)
 *
 * Created: 20251026 SUN
 * Updated: 20261007 WED
 * Authors: Zach Mahan, Ilan Goldfein
 */

#ifndef RADIO_SPP_H
#define RADIO_SPP_H

#include <stdbool.h>
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
// Sizes, in bytes, of the packet headers as they are sent
// Reference: SPP Blue Book pg. 4-2 (primary), pg. 4-7 (secondary, whose contents are mission-defined)
#define SPP_PRIMARY_HEADER_SIZE 6
#define SPP_SECONDARY_HEADER_CALLSIGN_SIZE 6
#define SPP_SECONDARY_HEADER_SIZE (8 + SPP_SECONDARY_HEADER_CALLSIGN_SIZE) // timestamp (4) + boot_timestamp (4) + callsign

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
    uint16_t application_process_id : 11; // "APID", indicates source, destination, or type
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
 *
 * It is present only when the primary header's secondary_header_flag is set, and is sent as
 * SPP_SECONDARY_HEADER_SIZE octets, in field order, with each integer most significant octet first
 */
typedef struct spp_secondary_packet_header {
    uint32_t timestamp;
    uint32_t boot_timestamp;
    uint8_t callsign[SPP_SECONDARY_HEADER_CALLSIGN_SIZE]; // sent as-is, in order
} spp_secondary_packet_header_t;

/*
 * standard spp packet with an in-place data field
 */
typedef struct spp_packet {
    spp_primary_packet_header_t header;
    spp_secondary_packet_header_t secondary_header; // only meaningful when header.secondary_header_flag is set
    uint8_t data[SPP_STANDARD_PACKET_SIZE];
} spp_packet_t;

/*
 * standard spp packet with a view into a buffer as its data field
 */
typedef struct spp_packet_view {
    spp_primary_packet_header_t header;
    spp_secondary_packet_header_t secondary_header; // only meaningful when header.secondary_header_flag is set
    void *data;
    // ^ the user data field, after the secondary header (if any). use header.data_length for bounds, but remember
    //   data_length = <number_of_bytes> - 1, and that it also counts the SPP_SECONDARY_HEADER_SIZE octets of the
    //   secondary header when one is present
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

/**
 * Parsing for SPP Space Packets
 *
 * - No copy (on success, view->data points into data, so data must outlive the view)
 * - Bytes in data past the end given by the packet data length field are ignored
 * - If the secondary header flag is set, the secondary header is parsed into view->secondary_header
 *   and view->data points just past it; otherwise view->secondary_header is zeroed
 *
 * \param view - packet view to fill in; left unspecified on failure
 * \param data - pointer to start of raw byte-stream data
 * \param len - length, in bytes, of the byte stream
 *
 * \return true on failure (malformed or unsupported), else false
 *
 * Reference: SPP Blue Book pg. 4-2 (primary header), pg. 4-7 (secondary header)
 */
bool spp_packet_parse(spp_packet_view_t *view, uint8_t *data, uint32_t len);

#endif // !RADIO_SPP_H
