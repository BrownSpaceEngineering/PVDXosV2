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

#ifdef UNITTEST
/// Length of the frame currently in tx_buffer, so tests can inspect what was sent
static uint32_t tx_len = 0;
#endif

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
uint32_t uslp_crc(const uint8_t *data, size_t length, bool is_crc32) {
    if (is_crc32) {
        // Standard CRC-32 (IEEE 802.3) polynomial representation
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < length; ++i) {
            crc ^= data[i];
            for (int j = 0; j < 8; ++j) {
                if (crc & 1) {
                    crc = (crc >> 1) ^ 0xEDB88320;
                } else {
                    crc >>= 1;
                }
            }
        }
        return ~crc;
    } else {
        // Standard CRC-16-CCITT polynomial representation (0x1021)
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < length; ++i) {
            crc ^= (uint16_t)((data[i]) << 8);
            for (int j = 0; j < 8; ++j) {
                if (crc & 0x8000) {
                    crc = (crc << 1) ^ 0x1021;
                } else {
                    crc <<= 1;
                }
            }
        }
        return crc;
    }
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
    //  PVDX is always destination (1)
    primary_header.src_or_dest = USLP_SCID_IS_DESTINATION;
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
        uint32_t crc = uslp_crc(tx_buffer, total_len - USLP_FECF_SIZE, /*is_crc32=*/USLP_IS_CRC32);
        if (USLP_IS_CRC32) {
            tx_buffer[total_len - 4] = crc >> 24;
            tx_buffer[total_len - 3] = crc >> 16;
            tx_buffer[total_len - 2] = crc >> 8;
            tx_buffer[total_len - 1] = crc & 0xFF;
        } else {
            tx_buffer[total_len - 2] = crc >> 8;
            tx_buffer[total_len - 1] = crc & 0xFF;
        }
    }

#ifdef UNITTEST
    tx_len = total_len;
#endif

    // TODO: send tx_buffer to comms
    return false;
}

bool uslp_transfer_frame_parse(uslp_transfer_frame_view_t *view, uint8_t *data, uint32_t len) {
    if (len < USLP_PRIMARY_HEADER_FIXED_SIZE) {
        return true; // too short to hold even the fixed part of the primary header
    }

    // ~~~ Transfer Frame Primary Header ~~~
    // The inverse of the packing in uslp_send: every field is most significant bit first
    uslp_transfer_frame_primary_header_t *ph = &view->primary_header;
    *ph = (uslp_transfer_frame_primary_header_t){0};

    ph->version_num = data[0] >> 4;
    if (ph->version_num != USLP_TFVN) {
        return true; // not a USLP frame
    }

    ph->spacecraft_id = ((data[0] & 0x0F) << 12) | (data[1] << 4) | (data[2] >> 4);
    ph->src_or_dest = (data[2] >> 3) & 0x01;
    ph->virtual_channel_id = ((data[2] & 0x07) << 3) | (data[3] >> 5);
    ph->map_id = (data[3] >> 1) & 0x0F;
    ph->end_of_frame_primary_header_flag = data[3] & 0x01;

    if (ph->end_of_frame_primary_header_flag) {
        return true; // truncated frames (Annex D) are not supported
    }

    ph->frame_length = (data[4] << 8) | data[5];
    ph->bypass_sequence_control_flag = data[6] >> 7;
    ph->protocol_control_command_flag = (data[6] >> 6) & 0x01;
    ph->spare = (data[6] >> 4) & 0x03;
    ph->ocf_flag = (data[6] >> 3) & 0x01;
    ph->vc_frame_count_length = data[6] & 0x07;

    // The frame length field holds the total number of octets in the frame minus one
    // Reference: USLP Blue Book 4.1.2.7.2
    uint32_t frame_len = (uint32_t)ph->frame_length + 1;
    if (frame_len > len) {
        return true; // the frame claims to be longer than the data we were given
    }

    uint8_t count_len = ph->vc_frame_count_length;
    uint32_t header_len = USLP_PRIMARY_HEADER_FIXED_SIZE + count_len + USLP_DATA_FIELD_HEADER_SIZE;
    uint32_t trailer_len = (ph->ocf_flag ? USLP_OCF_SIZE : 0) + USLP_FECF_SIZE;

    if (header_len + trailer_len > frame_len) {
        return true; // too short to hold the headers and trailer it says it has
    }

    // ~~~ Frame Error Control Field ~~~
    // Checked before trusting anything else in the frame
    if (USLP_FECF_SIZE > 0) {
        uint32_t received_crc = 0;
        if (USLP_IS_CRC32) {
            received_crc = (data[frame_len - 4] << 24) | (data[frame_len - 3] << 16) | (data[frame_len - 2] << 8) | data[frame_len - 1];
        } else {
            received_crc = (data[frame_len - 2] << 8) | data[frame_len - 1];
        }
        if (uslp_crc(data, frame_len - USLP_FECF_SIZE, /*is_crc32=*/USLP_IS_CRC32) != received_crc) {
            return true;
        }
    }

    // VC frame count, most significant octet first
    for (uint8_t i = 0; i < count_len; i++) {
        ph->vc_frame_count = (ph->vc_frame_count << 8) | data[USLP_PRIMARY_HEADER_FIXED_SIZE + i];
    }

    // ~~~ Transfer Frame Data Field Header ~~~
    uint8_t dfh = data[USLP_PRIMARY_HEADER_FIXED_SIZE + count_len];
    view->data_field_header.tfdz_construction_rules = dfh >> 5;
    view->data_field_header.protocol_identifier = dfh & 0x1F;

    if (view->data_field_header.tfdz_construction_rules <= USLP_TFDZ_CONTINUING_PORTION_OF_MAPA_SDU) {
        return true; // rules 000-010 add a 16-bit First Header / Last Valid Octet pointer, which is not supported
    }

    // ~~~ Transfer Frame Data Zone ~~~
    // Everything between the data field header and the OCF/FECF
    view->datafield = &data[header_len];
    view->datafield_len = frame_len - header_len - trailer_len;

    return false;
}

#ifdef UNITTEST
const uint8_t *uslp_test_last_frame(uint32_t *len) {
    *len = tx_len;
    return tx_buffer;
}
#endif
