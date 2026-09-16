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

int uslp_mapp_request(uint8_t *sdu, uint16_t sdu_len, uint32_t gmap_id, uint8_t pvn, uint32_t sdu_id, uslp_qos_t qos) {
    // GMAP ID = TFVN (4) | SCID (16) | VCID (6) | MAP ID (4) = 30 bits
    // tfvn is bits 29-26 (4 bit mask)
    uint8_t tfvn = (gmap_id >> 26) & 0x0F;
    // scid is bits 25-10 (16 bit mask)
    uint16_t scid = (gmap_id >> 10) & 0xFFFF;
    // vcid is bits 9-4 (6 bit mask)
    uint8_t vcid = (gmap_id >> 4) & 0x3F;
    // map_id is bits 3-0 (4 bit mask)
    uint8_t map_id = gmap_id & 0x0F;

    if (tfvn != USLP_TFVN) {
        return -1; // USLP TFVN always needs to be 1100
    }

    // Create the primary header based off decoded fields
    uslp_transfer_frame_primary_header_t primary_header = {0};
    primary_header.version_num = USLP_TFVN;
    primary_header.spacecraft_id = scid;
    // Frames sent by PVDX carry PVDX's own SCID, so the SCID is the source
    primary_header.src_or_dest = USLP_SCID_IS_SOURCE;
    primary_header.virtual_channel_id = vcid;
    primary_header.map_id = map_id;
    // Always 0 here: 1 would mean a truncated frame (Annex D), which MAPP never sends
    primary_header.end_of_frame_primary_header_flag = 0;
    // --------------- Other fields ----------------------------

    primary_header.vc_frame_count = vc_frame_counts[vcid]++; // This needs to be some sort of counter which increments per frame sent
    // TODO: see if these need non-zero values
    primary_header.frame_length = 0;

    if (qos == USLP_QOS_SEQUENCE_CONTROLLED) {
        return -1; // Only expedited works - retransmit is not impleneted within USLP
    }

    primary_header.bypass_sequence_control_flag = qos;
    primary_header.protocol_control_command_flag = 0;
    primary_header.spare = 0;
    primary_header.ocf_flag = 0;
    primary_header.vc_frame_count_length = 0;

    // Create the data field header
    uslp_transfer_frame_data_field_header_t data_header = {0};
    data_header.tfdz_construction_rules = USLP_TFDZ_NO_SEGMENT;
    data_header.protocol_identifier = USLP_UPID_SPACE_PACKETS;

    uslp_transfer_frame_view_t frame = {0};
    frame.primary_header = primary_header;
    frame.data_field_header = data_header;
    frame.datafield = sdu;
    frame.datafield_len = sdu_len;

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
