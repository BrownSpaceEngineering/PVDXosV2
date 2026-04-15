#include "cfdp_engine.h"

#include "cfdp_pdu.h"
#include "string.h"

int cfdp_send_init(uint8_t *fl, uint32_t sz, cfdp_lv_t source_filename, cfdp_lv_t dest_filename, uint32_t source_entity_id,
                   uint32_t dest_entity_id, uint8_t channel_num, uint8_t priority, bool reliable_mode) {
    if (fl == NULL || sz == 0)
        return -1;

    cfdp_gap_tracker_t gap_tracker = {.bitmap = {0}, .file_size = sz, .bytes_recv = 0, .eof_recv = false};

    cfdp_transaction_id_init transaction_id = {.entity_id = source_entity_id, .seq_num = next_seq_num()};

    cfdp_transaction_t transaction = {.transaction_id = &transaction_id,
                                      .dest_entity_id = dest_entity_id,
                                      .inactivity_timer = 0,
                                      .ack_timer = 0,
                                      .gaps = NULL,
                                      .state = CFDP_SEND_STATE_METADATA_SEND,
                                      .direction = CFDP_SEND,
                                      .reliable_mode = reliable_mode,
                                      .channel_num = channel_num,
                                      .priority = priority,
                                      .file_data = fl,
                                      .source_filename = source_filename,
                                      .dest_filename = dest_filename};
    return 0;
}

void cfdp_handle_send_state(cfdp_transaction_t *transaction) {
    switch (transaction->state) {
        case CFPD_SEND_STATE_METADATA_SEND:
            cfdp_send_metadata(transaction);
            break;
        case CFPD_SEND_STATE_FILE_SEND:
            if (transaction->gaps->bytes_recv > 0) {
                cfdp_resend(transaction);
            } else if (transaction->file_offset < transaction->file_size) {
                cfdp_send_next_filedata(transaction);
            } else {
                cfdp_send_eof(transaction);
            }
            break;
        case CFDP_SEND_STATE_WAIT_ACK:
            // do anything?
            break;
        case CFDP_SEND_STATE_WAIT_FIN:
            // do anything?
            break;
        case CFDP_SEND_STATE_ERR:
            // panic? or just fail silently?
            break;
        default:
            // panic! A send transaction should always be one of these!
    }
}

void uint32_to_big_endian(uint32_t src, uint8_t *dst) {
    dst[0] = (src >> 24) & 0xFF;
    dst[1] = (src >> 16) & 0xFF;
    dst[2] = (src >> 8) & 0xFF;
    dst[3] = src & 0xFF;
}

void uint16_to_big_endian(uint16_t src, uint8_t *dst) {
    dst[0] = (src >> 8) & 0xFF;
    dst[1] = src & 0xFF;
}

int cfdp_prepare_pdu_header(uint8_t *buff, cfdp_transaction_t *transaction, uint16_t pdu_len, cfdp_pdu_type_t pdu_type) {
    if (buff == NULL || transaction == NULL || pdu_len == 0)
        return -1;

    uint8_t direction = (transaction->direction == CFDP_SEND) ? 1 : 0;
    uint8_t mode = (transaction->reliable_mode) ? 0 : 1;
    uint8_t crc_present = 0; // does USLP handle this?
    uint8_t large_file = 0;

    buff[0] = (0b001 << 5) | ((pdu_type & 0b1) << 4) | (direction << 3) | (mode << 2) | (crc_present << 1) | large_file;

    uint16_to_big_endian(pdu_len, buff + 1);

    uint8_t has_segment_metadata = 0;
    buff[3] = 0b00110011 | (has_segment_metadata << 3); // sets both entity ID/trans sequence length to 3 (4 octets)

    uint32_to_big_endian(transaction->transaction_id->entity_id, buff + 4);

    uint32_to_big_endian(transaction->transaction_id->seq_num, buff + 8);

    uint32_to_big_endian(transaction->dest_entity_id, buff + 12);

    return 0;
}

int cfdp_send_metadata(cfdp_transaction_t *transaction) {
    if (transaction == NULL)
        return 0;

    size_t source_filename_length = transaction->source_filename.length;
    size_t dest_filename_length = transaction->dest_filename.length;
    size_t metadata_size = 7 + source_filename_length + dest_filename_length;

    uint8_t buff[metadata_size + 16];
    cfdp_prepare_pdu_header(buff, transaction, metadata_size & 0xFFFF, CFDP_FILE_DIRECTIVE);

    uint8_t *metadata_buff = buff + 16;

    metadata_buff[0] = 0;

    uint8_t req_closure = (transaction->reliable_mode) ? 0 : REQ_CLOSURE;
    metadata_buff[0] |= (req_closure << 6);
    metadata_buff[0] |= CHECKSUM_TYPE;

    uint32_to_big_endian(transaction->file_size, metadata_buff + 1);

    metadata_buff[5] = source_filename_length;
    for (int i = 0; i < source_filename_length; i++) {
        metadata_buff[6 + i] = (transaction->source_filename.value)[i];
    }

    metadata_buff[6 + source_filename_length] = (dest_filename_length);
    for (int i = 0; i < dest_filename_length; i++) {
        metadata_buff[7 + i + source_filename_length] = (transaction->source_filename.value)[i];
    }

    send(buff, metadata_size + 16);
    return 0;
}

// Note: For now this does not support segment metadata
int cfdp_send_filedata(cfdp_transaction_t *transaction, uint32_t offset) {
    if (transaction == NULL)
        return -1;

    uint8_t buff[20 + SEGMENT_SIZE];
    cfdp_prepare_pdu_header(buff, transaction, 4 + SEGMENT_SIZE, CFDP_FILE_DATA);

    uint8_t *filedata_buff = buff + 16;

    uint32_to_big_endian(offset, filedata_buff);

    memcpy(filedata_buff + 4, transaction->file_data + offset, SEGMENT_SIZE);

    send(buff, 20 + SEGMENT_SIZE);
    return 0;
}

int cfdp_send_next_filedata(cfdp_transaction_t *transaction) {
    cfdp_send_filedata(transaction, transaction->file_offset);
}

int cfdp_send_eof(cfdp_transaction_t *transaction, uint8_t condition_code) {
    size_t fault_location_size = 0;
    if (condition_code != CFDP_COND_NOERROR) {
        fault_location_size = 6;
    }
    uint8_t buff[25 + fault_location_size];

    cfdp_prepare_pdu_header(buff, transaction, 9, CFDP_FILE_DIRECTIVE);

    uint8_t *eof_buff = buff + 16;

    eof_buff[0] = (condition_code & 0xF) << 4;
    uint32_to_big_endian(get_checksum(transaction), eof_buff + 1);
    uint32_to_big_endian(transaction->file_offset, eof_buff + 5);

    // We're making all entity IDs 4 Bytes, but we still have to encode TLV Format
    if (condition_code != CFDP_COND_NOERROR) {
        eof_buff[9] = 0x06;
        eof_buff[10] = 0x04;
        uint32_to_big_endian(transaction->transaction_id->entity_id, eof_buff + 11);
    }
    send(buff, 25 + fault_location_size);
    return 0;
}

int cfdp_resend(cfdp_transaction_t *transaction) {
    uint32_t total_segs = (transaction->gaps->file_size) / SEGMENT_SIZE;
    bool in_gap = false;

    for (uint32_t seg = 0; seg < total_segs; seg++) {}
}
