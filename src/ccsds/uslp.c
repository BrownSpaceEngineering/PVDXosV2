/**
 * src/ccsds/uslp.c
 *
 * Implementation of the CCSDS Unified Space Data Link Protocol (USLP)
 *
 * Created: 20260503 SUN
 * Updated: 20260503 SUN
 * Authors: Ilan Goldfein
 */

#include "uslp.h"

static int uslp_send(uslp_transfer_frame_view_t *view); // Declaring uslp_send() function

int uslp_mapp_request(uint8_t *sdu, uint32_t gmap_id, uint8_t pvn, uint32_t sdu_id, uslp_qos_t qos) {
    // tfvn is bits 31-28 (4 bit mask)
    uint16_t tfvn = (gmap_id >> 28) & 0x0F;
    // scid is bits 27-12 (16 bit mask)
    uint16_t scid = (gmap_id >> 12) & 0xFFFF;
    // src_or_dest_id is at bit 11 (one bit mask)
    uint8_t src_or_dest_id = (gmap_id >> 11) & 0x01;
    // vcid is bits 10-5 (6 bit mask)
    uint8_t vcid = (gmap_id >> 5) & 0x3F;
    // map_id is bits 4-1 (4 bit mask)
    uint8_t map_id = (gmap_id >> 1) & 0x0F;
    // header flag is the first bit (one bit mask)
    uint8_t header_flag = gmap_id & 0x01;

    // Create the primary header based off decoded fields
    uslp_transfer_frame_primary_header_t primary_header = {0};
    primary_header.version_num = tfvn;
    primary_header.spacecraft_id = scid;
    primary_header.src_or_dest = src_or_dest_id;
    primary_header.virtual_channel_id = vcid;
    primary_header.map_id = map_id;
    primary_header.end_of_frame_primary_header_flag = header_flag;
    // --------------- Other fields ----------------------------
    // TODO: see if these need non-zero values
    primary_header.vc_frame_count = 0; // This needs to be some sort of counter which increments per frame sent
    primary_header.frame_length = 0;
    primary_header.bypass_sequence_control_flag = qos;
    primary_header.protocol_control_command_flag = 0;
    primary_header.spare = 0;
    primary_header.ocf_flag = 0;
    primary_header.vc_frame_count_length = 0;

    // Create the data field header
    uslp_transfer_frame_data_field_header_t data_header = {0};
    data_header.tfdz_construction_rules = USLP_TFDZ_PACKETS_SPAN_MULTIPLE_FRAME;
    data_header.protocol_identifier = 0; // Maybe?

    uslp_transfer_frame_view_t frame = {0};
    frame.primary_header = primary_header;
    frame.data_field_header = data_header;
    frame.datafield = sdu;

    return uslp_send(&frame);
}

/**
 * Function for serializing the USLP transfer frame from the internal representation
 * into the representation for sending over radio
 */
static int uslp_send(uslp_transfer_frame_view_t *view) {
    return 0;
}

bool uslp_transfer_frame_parse(uslp_transfer_frame_t *tf, uint8_t *data, uint32_t len) {
    return false;
}
