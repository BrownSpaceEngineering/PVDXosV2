#include "cfdp_engine.h"

#include "cfdp_pdu.h"
#include "string.h"
/*""
    "TODOS:
    1. Add in timer logic

    2. Ensure all macro numbering is proper

    3. Transact function that handles everything in a single flow

    4. ""
    "*/

// note : this currently doesn't build but i dont have time to fix it rn...

/**
 * cfdp_send_init
 *
 * \brief initialises the data structures to manage a CFDP transaction
 *
 * \param fl, an array of uint8_t representing the bytes of a file
 * \param sz, the size of the file in bytes
 * \param source_filename, the representation of file on this device
 * \param dest_filename, the representationg of file on receiving device
 * \param source_entity_id, the entity ID of this device
 * \param dest_entity_id, the entity ID of the destination device
 * \param channel_num, the channel this transaction will take place over
 * \param priority, this transation's priority
 * \param reliable_mode, whether this transaction should occur in reliable mode
 *
 * \return int, 0 on success
 */
int cfdp_send_init(uint8_t *fl, size_t sz, cfdp_lv_t source_filename, cfdp_lv_t dest_filename, uint32_t source_entity_id,
                   uint32_t dest_entity_id, uint8_t channel_num, uint8_t priority, bool reliable_mode) {
    if (fl == NULL || sz == 0) {
        return -1;
    }

    cfdp_pdu_segment_request_t segments[CFDP_MAX_SEGMENT_REQUESTS];

    cfdp_nak_buf_t nak_buf = {.segments = segments, .head = 0, .tail = 0, .size = 0};

    cfdp_transaction_id_init transaction_id = {.entity_id = source_entity_id, .seq_num = next_seq_num()};

    cfdp_transaction_t transaction = {.transaction_id = &transaction_id,
                                      .dest_entity_id = dest_entity_id,
                                      .inactivity_timer = 0,
                                      .ack_timer = 0,
                                      .nak_buf = nak_buf,
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

int cfdp_send_init_simple(uint8_t *fl, size_t sz) {
    cfdp_lv_t empty = {.length = 0, .value = NULL};
    return cfdp_send_init(fl, sz,
                          empty,                // source_file
                          empty,                // dest_file
                          ENTITY_ID_SPACECRAFT, // source_entity_id
                          ENTITY_ID_GROUND,     // dest_entity_id
                          0,                    // channel_num
                          0,                    // priority
                          true                  // reliable_mode
    );
}

void cfdp_handle_send_state(cfdp_transaction_t *transaction) {
    switch (transaction->state) {
        case CFPD_SEND_STATE_METADATA_SEND:
            cfdp_send_metadata(transaction);
            break;
        case CFPD_SEND_STATE_FILE_SEND:
            if (transaction->nak_buf->count > 0) { // if there are NAKs we need to respond to
                cfdp_resend(transaction);
            } else if (transaction->file_offset < transaction->file_size) {
                cfdp_send_filedata(transaction, transaction->file_offset, SEGMENT_SIZE); // segments size is a bit of a placeholder
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

/**
 *List of bit conversion functions
 *
 * */
void uint32_to_big_endian(uint32_t src, uint8_t dst[4]) {
    dst[0] = (src >> 24) & 0xFF;
    dst[1] = (src >> 16) & 0xFF;
    dst[2] = (src >> 8) & 0xFF;
    dst[3] = src & 0xFF;
}

void uint16_to_big_endian(uint16_t src, uint8_t dst[2]) {
    dst[0] = (src >> 8) & 0xFF;
    dst[1] = src & 0xFF;
}

/**
 * Functions for interfacing with the NAK Buffer
 */
void cfdp_nak_buf_push(cfdp_nak_buf_t *buf, cfdp_pdu_segment_request_t segment) {
    if (buf->size == 0) {
        buf->size = 1;
        buf->tail = 0;
        buf->head = 0;
        buf->segments[0] = segment;
        return;
    }

    // overwrites oldest data
    if (buf->size == MAX_NAK_COUNT) {
        buf->head = (buf->head + 1) % MAX_NAK_COUNT;
        buf->tail = (buf->tail + 1) % MAX_NAK_COUNT;
        buf->segments[buf->head] = segment;
    }

    buf->head = (buf->head + 1) % MAX_NAK_COUNT;
    buf->segments[buf->head] = segment;
    buf->size += 1;
}

cfdp_pdu_segment_request_t cfdp_nak_buf_pop(cfdp_nak_buf_t *buf) {
    if (buf->size == 0) {
        return {.start_offset = ((uint32_t)-1), .end_offset = ((uint32_t)-1)};
    }

    cfdp_pdu_segment_request_t seg = buf->segments[buf->tail];
    buf->tail = (buf->tail + 1) % MAX_NAK_COUNT;
    buf->size -= 1;
    return seg;
}

/**
 *
 *
 */
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
    transaction->state = CFDP_SEND_FILE_SEND;
    return 0;
}

// Note: For now this does not support segment metadata
int cfdp_send_filedata(cfdp_transaction_t *transaction, uint32_t offset, uint32_t size) {
    if (transaction == NULL)
        return -1;

    uint8_t buff[20 + size];
    cfdp_prepare_pdu_header(buff, transaction, 4 + size, CFDP_FILE_DATA);

    uint8_t *filedata_buff = buff + 16;

    uint32_to_big_endian(offset, filedata_buff);

    memcpy(filedata_buff + 4, transaction->file_data + offset, size);

    send(buff, 20 + size);

    transaction->file_offset += size;
    return size;
}

uint32_t cfdp_calculate_modular_checksum(cfdp_transaction_t *txn) {
    size_t words = txn->file_size / 4;
    uint32_t checksum = 0;

    for (uint32_t i = 0; i < words * 4; i += 4) {
        checksum += (((uint32_t)txn->file_data[i] << 24) | ((uint32_t)txn->file_data[i + 1] << 16) |
                     ((uint32_t)txn->file_data[i + 2] << 8) | txn->file_data[i + 3]);
    }

    size_t rem = txn->file_size % 4;

    uint32_t checksum_rem = 0;
    for (uint32_t i = 0; i < rem; i++) {
        checksum_rem |= (uint32_t)txn->file_data[words * 4 + i] << (8 * (3 - i));
    }

    return checksum + checksum_rem;
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
    uint32_to_big_endian(cfdp_calc(transaction), eof_buff + 1);
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
    while (transaction->nak_buf.size > 0) {
        cfdp_pdu_segment_request_t seg = cfdp_nak_buf_pop(transaction->nak_buf.segments);

        uint32_t nak_size = seg.end_offset - seg.start_offset;
        uint32_t resend_count = nak_size / SEGMENT_SIZE;

        for (uint32_t i = 0; i < resend_count; ++i) {
            cfdp_send_filedata(transaction, seg.start_offset + i * SEGMENT_SIZE, SEGMENT_SIZE);
        }

        if (nak_size % SEGMENT_SIZE > 0) {
            cfdp_send_filedata(transaction, seg.start_offset + resend_count * SEGMENT_SIZE, nak_size % SEGMENT_SIZE);
        }
    }
    return 0;
}

cfdp_result_t cfdp_transact(cfdp_transaction_t *txn, uint32_t elapsed_ms) {}
