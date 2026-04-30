/**
 * src/ccsds/uslp.h
 *
 * header file for the PVDX implementation of the CCSDS Unified Space Data Link Protocol (USLP)
 *
 * Created: 20260429 SUN
 * Updated: 20260429 THU
 * Authors: Zach Mahan
 */

#include <stdint.h>

/// USLP Transfer Frame Primary Header
///
/// Reference: USLP Green & Blue Book
///
/// - This struct internally reorders some fields and using bitfields,
///   so the ordering does not necessarily match the specification
/// - This struct is intended to be used for internal representation in
///   the OS and should not be transmitted over comms in any way
typedef struct uslp_transfer_frame_primary_header {
    uint64_t vc_frame_count : 56;
    uint16_t spacecraft_id;
    uint16_t frame_length;
    uint8_t version_num : 4;
    uint8_t src_or_dest : 1;
    uint8_t virtual_channel_id : 6;
    uint8_t map_id : 4;
    uint8_t end_of_frame_primary_header_flag : 1;
    uint8_t bypass_sequence_control_flag : 1;
    uint8_t protocol_control_command_flag : 1;
    uint8_t spare : 2;
    uint8_t ocf_flag : 1;
    uint8_t vc_frame_count_length : 3;
} uslp_transfer_frame_primary_header_t;

/// USLP Transfer Frame Data Field Header
///
/// Reference: USLP Green & Blue Book
///
/// - This struct internally reorders some fields and using bitfields,
///   so the ordering does not necessarily match the specification
/// - This struct is intended to be used for internal representation in
///   the OS and should not be transmitted over comms in any way
typedef struct uslp_transfer_frame_data_field_header {
    /// Transer Frame Data Zone (TFDZ) Construction Rules
    uint8_t tfdz_construction_rules : 3;
    uint8_t protocol_identifier : 5;
} uslp_transfer_frame_data_field_header_t;

// ~~~ Rules for Transer Frame Data Zone (TFDZ) Construction Rules ~~~

#define USLP_TFDZ_PACKETS_SPAN_MULTIPLE_FRAME 0b000
#define USLP_TFDZ_COMPLETE_OR_PORTION_OF_MAPA_SDU 0b001
#define USLP_TFDZ_CONTINUING_PORTION_OF_MAPA_SDU 0b010
#define USLP_TFDZ_BYTE_STREAM 0b011
#define USLP_TFDZ_STARTING_SEGMENT 0b100
#define USLP_TFDZ_CONTINUING_SEGMENT 0b101
#define USLP_TFDZ_LAST_SEGMENT 0b110
#define USLP_TFDZ_NO_SEGMENT 0b111

// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

// USLP Transer Frame with a flexible array memeber as the data field
typedef struct uslp_transfer_frame {
    uslp_transfer_frame_primary_header_t primary_header;
    uslp_transfer_frame_data_field_header_t data_field_header;
    uint8_t datafield[];
} uslp_transfer_frame_t;

// USLP Transer Frame with a pointer as the data field
typedef struct uslp_transfer_frame_view {
    uslp_transfer_frame_primary_header_t primary_header;
    uslp_transfer_frame_data_field_header_t data_field_header;
    uint8_t *datafield;
} uslp_transfer_frame_view_t;
