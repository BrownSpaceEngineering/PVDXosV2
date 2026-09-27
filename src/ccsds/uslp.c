/**
 * src/ccsds/uslp.c
 *
 * Implementation of the CCSDS Unified Space Data Link Protocol (USLP)
 *
 * Created: 20260503 SUN
 * Updated: 20260927 SUN
 * Authors: Ilan Goldfein, Zach Mahan
 */

#include "uslp.h"

#include <string.h>

#include "mutexes.h"
#include "spp.h" // for SPP_VERSION_NUMBER

/// Counter for number of frames for each of the 64 possible vc channels
static uint8_t vc_frame_counts[USLP_VIRTUAL_CHANNEL_COUNT];

/// Frames are serialized in place here before being handed to the radio, so that a
/// full-size frame does not have to live on a task's stack.
static uint8_t tx_buffer[USLP_MAX_FRAME_SIZE];

/// Guards tx_buffer and vc_frame_counts, which are shared by every task that sends
static SemaphoreHandle_t uslp_tx_mutex = NULL;
static StaticSemaphore_t uslp_tx_mutex_buffer;

static bool uslp_send(uslp_transfer_frame_view_t *view);

/**
 * CRC used by the Frame Error Control Field: generator X^16 + X^12 + X^5 + 1 (0x1021), with the
 * shift register preset to all ones, most significant bit first, and no final inversion.
 * This is a software copy of the shift register in Figure B-1.
 *
 * Reference: USLP Blue Book Annex B
 */
static uint16_t uslp_crc16(const uint8_t *data, uint32_t len) {
    uint16_t crc = 0xFFFF;

    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }

    return crc;
}

void uslp_init(void) {
    uslp_tx_mutex = xSemaphoreCreateMutexStatic(&uslp_tx_mutex_buffer);

    if (uslp_tx_mutex == NULL) {
        fatal("Failed to create USLP tx mutex");
    }
}

bool uslp_mapp_request(uint8_t *sdu, uint16_t sdu_len, uint32_t gmap_id, uint8_t pvn, uint32_t sdu_id, uslp_qos_t qos) {
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
        return true; // USLP TFVN always needs to be 1100
    }

    if (vcid == USLP_IDLE_ONLY_FRAME_INDEX) {
        return true; // VC 63 is reserved for Only Idle Data frames and may not carry user data
    }

    if (pvn != SPP_VERSION_NUMBER) {
        return true; // Space Packets are the only packet type PVDX sends, so the UPID below assumes them
    }

    if (qos == USLP_QOS_SEQUENCE_CONTROLLED) {
        return true; // Only expedited works - retransmit is not implemented within USLP
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

    primary_header.frame_length = 0;

    primary_header.bypass_sequence_control_flag = qos;
    primary_header.protocol_control_command_flag = 0;
    primary_header.spare = 0;
    primary_header.ocf_flag = 0;
    primary_header.vc_frame_count_length = 1;

    // Create the data field header
    uslp_transfer_frame_data_field_header_t data_header = {0};
    data_header.tfdz_construction_rules = USLP_TFDZ_NO_SEGMENT;
    data_header.protocol_identifier = USLP_UPID_SPACE_PACKETS;

    uslp_transfer_frame_view_t frame = {0};
    frame.data_field_header = data_header;
    frame.datafield = sdu;
    frame.datafield_len = sdu_len;

    // The frame count is read and advanced under the same lock as tx_buffer, so that two tasks
    // sending on the same VC can never stamp their frames with the same count
    lock_mutex(uslp_tx_mutex);

    primary_header.vc_frame_count = vc_frame_counts[vcid];
    frame.primary_header = primary_header;

    bool err = uslp_send(&frame);
    if (!err) {
        // Only frames that were actually sent use up a count, so rejected frames don't leave gaps
        vc_frame_counts[vcid]++;
    }

    unlock_mutex(uslp_tx_mutex);

    return err;
}

/**
 * Function for serializing the USLP transfer frame from the internal representation
 * into the representation for sending over radio
 *
 * NOTE: the caller must hold uslp_tx_mutex, since this writes into the shared tx_buffer
 */
static bool uslp_send(uslp_transfer_frame_view_t *view) {
    const uslp_transfer_frame_primary_header_t *ph = &view->primary_header;
    uint8_t count_len = ph->vc_frame_count_length; // number of octets the VC frame count occupies

    uint16_t header_len = USLP_PRIMARY_HEADER_FIXED_SIZE + count_len + USLP_DATA_FIELD_HEADER_SIZE;
    uint32_t total_len = header_len + view->datafield_len + USLP_FECF_SIZE;

    if (total_len > USLP_MAX_FRAME_SIZE) {
        return true; // too large for one frame, and segmentation is not implemented yet
    }

    // ~~~ Transfer Frame Primary Header ~~~
    // Every field is packed most significant bit first, so each octet is built by
    // masking each field down to its width and shifting it into position
    tx_buffer[0] = (ph->version_num << 4) | (ph->spacecraft_id >> 12);
    tx_buffer[1] = (ph->spacecraft_id >> 4) & 0xFF;
    tx_buffer[2] = ((ph->spacecraft_id & 0x0F) << 4) | (ph->src_or_dest << 3) | (ph->virtual_channel_id >> 3);
    tx_buffer[3] = ((ph->virtual_channel_id & 0x07) << 5) | (ph->map_id << 1) | ph->end_of_frame_primary_header_flag;
    // Octets 4-5 hold the frame length, which is filled in once the total length is known
    tx_buffer[6] = (ph->bypass_sequence_control_flag << 7) | (ph->protocol_control_command_flag << 6) | (ph->spare << 4) |
        (ph->ocf_flag << 3) | count_len;

    // VC frame count, most significant octet first
    for (uint8_t i = 0; i < count_len; i++) {
        tx_buffer[USLP_PRIMARY_HEADER_FIXED_SIZE + i] = (ph->vc_frame_count >> (8 * (count_len - 1 - i))) & 0xFF;
    }

    // ~~~ Transfer Frame Data Field Header ~~~
    tx_buffer[USLP_PRIMARY_HEADER_FIXED_SIZE + count_len] =
        (view->data_field_header.tfdz_construction_rules << 5) | view->data_field_header.protocol_identifier;

    // ~~~ Transfer Frame Data Zone ~~~
    memcpy(&tx_buffer[header_len], view->datafield, view->datafield_len);

    // The frame length field holds the total number of octets in the frame minus one
    // Reference: USLP Blue Book 4.1.2.7.2
    uint16_t frame_length = total_len - 1;
    tx_buffer[4] = frame_length >> 8;
    tx_buffer[5] = frame_length & 0xFF;

    // ~~~ Frame Error Control Field ~~~
    // The CRC covers every octet of the frame ahead of it, so it has to be computed last, once
    // the frame length has been filled in
    if (USLP_FECF_SIZE > 0) {
        uint16_t crc = uslp_crc16(tx_buffer, total_len - USLP_FECF_SIZE);
        tx_buffer[total_len - 2] = crc >> 8;
        tx_buffer[total_len - 1] = crc & 0xFF;
    }

    // TODO: send tx_buffer to comms
    return false;
}

bool uslp_transfer_frame_parse(uslp_transfer_frame_t *tf, uint8_t *data, uint32_t len) {
    return false;
}
