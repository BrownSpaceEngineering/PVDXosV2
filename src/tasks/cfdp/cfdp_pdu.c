#include "cfdp_pdu.h"

#include "cfdp_task.h"

void cfdp_data_view_clear_data(cfdp_data_view_t *view) {
    view->data = NULL;
    view->len = 0;
}

// Returns -1 on error, bytes parsed on success
int cfdp_pdu_header_parse(const uint8_t *raw, size_t len, cfdp_pdu_header_t *out) {
    if (raw == NULL || out == NULL)
        return -1;
    if (len < 4)
        return -1;

    out->version_number = (raw[0] >> 5) & 0x07;
    out->pdu_type = (raw[0] >> 4) & 0x01;
    out->direction = (raw[0] >> 3) & 0x01;
    out->transmission_mode = (raw[0] >> 2) & 0x01;
    out->crc = (raw[0] >> 1) & 0x01;
    out->largefile = raw[0] & 0x01;

    out->pdu_data_length = ((uint16_t)raw[1] << 8) | raw[2];

    out->segmentation_control = (raw[3] >> 7) & 0x01;
    out->entity_id_len = ((raw[3] >> 4) & 0x07) + 1;
    out->segment_metadata_field = (raw[3] >> 3) & 0x01;
    uint8_t seq_len = (raw[3] & 0x07) + 1;

    size_t header_len = 16; // why do we hardcode this, isnt this meant to be header_len=4+entity_id_len+seq_len+entity_id_len

    if (len < header_len)
        return -1;

    const uint8_t *pos = raw + 4;

    out->source_entity_id = 0;
    for (size_t i = 0; i < 4 && i < out->entity_id_len; i++) {
        out->source_entity_id |= (uint32_t)pos[i] << (24 - i * 8);
    }
    pos += out->entity_id_len;

    out->transaction_seq = 0;
    for (size_t i = 0; i < 4 && i < seq_len; i++) {
        out->transaction_seq |= (uint32_t)pos[i] << (24 - i * 8);
    }
    pos += seq_len;

    out->dest_entity_id = 0;
    for (size_t i = 0; i < 4 && i < out->entity_id_len; i++) {
        out->dest_entity_id |= (uint32_t)pos[i] << (24 - i * 8);
    }

    return (int)header_len;
}

// Returns -1 on error, bytes parsed on success
int cfdp_pdu_metadata_parse(const uint8_t *raw, size_t len, cfdp_pdu_metadata_t *out) {
    if (raw == NULL || out == NULL)
        return -1;
    if (len < 7)
        return -1;

    out->closure_req = (raw[0] >> 6) & 0x01;
    out->checksum_type = raw[0] & 0x0F;

    out->file_length = ((uint32_t)raw[1] << 24) | ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 8) | raw[4];

    uint8_t src_len = raw[5];
    if ((uint8_t)len < 6 + src_len + 1)
        return -1;
    out->source_id = (src_len > 0) ? raw[6] : 0;

    size_t dest_offset = 6 + src_len;
    uint8_t dest_len = raw[dest_offset];
    if (len < dest_offset + 1 + dest_len)
        return -1;
    out->dest_id = (dest_len > 0) ? raw[dest_offset + 1] : 0;

    size_t consumed = dest_offset + 1 + dest_len;
    if (len > consumed) {
        cfdp_view_init(&out->options, raw + consumed, len - consumed);
    } else {
        cfdp_view_init_empty(&out->options);
    }

    return (int)len;
}

// Returns -1 on error, bytes parsed on success
int cfdp_pdu_filedata_parse(const uint8_t *raw, size_t len, bool large_file, bool has_segment_metadata, cfdp_pdu_filedata_t *out) {
    if (raw == NULL || out == NULL)
        return -1;

    size_t offset_size = large_file ? 8 : 4;
    const uint8_t *pos = raw;

    out->continue_state = 0;
    out->segment_metadata_length = 0;
    cfdp_view_init_empty(&out->segment_metadata);

    if (has_segment_metadata) {
        if (len < 1)
            return -1;

        out->continue_state = (pos[0] >> 6) & 0x03;
        out->segment_metadata_length = pos[0] & 0x3F;
        pos++;

        if (len < 1 + out->segment_metadata_length + offset_size)
            return -1;

        cfdp_view_init(&out->segment_metadata, pos, out->segment_metadata_length);
        pos += out->segment_metadata_length;
    } else {
        if (len < offset_size)
            return -1;
    }

    out->offset = 0;
    for (size_t i = 0; i < offset_size; i++) {
        out->offset = (out->offset << 8) | pos[i];
    }
    pos += offset_size;

    size_t header_size = pos - raw;
    if (len > header_size) {
        cfdp_view_init(&out->data, pos, len - header_size);
    } else {
        cfdp_view_init_empty(&out->data);
    }

    return (int)len;
}

// Returns -1 on error, bytes parsed on success
int cfdp_pdu_eof_parse(const uint8_t *raw, size_t len, bool large_file, cfdp_pdu_eof_t *out) {
    if (raw == NULL || out == NULL)
        return -1;

    size_t filesize_len = large_file ? 8 : 4;
    size_t min_len = 1 + 4 + filesize_len;

    if (len < min_len)
        return -1;

    out->condition_code = (raw[0] >> 4) & 0x0F;

    out->checksum = ((uint32_t)raw[1] << 24) | ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 8) | raw[4];

    out->filesize = 0;
    for (size_t i = 0; i < filesize_len; i++) {
        out->filesize = (out->filesize << 8) | raw[5 + i];
    }

    cfdp_view_init_empty(&out->fault_entity_id);
    size_t tlv_offset = 5 + filesize_len;
    if (out->condition_code != CFDP_COND_NOERROR && len >= tlv_offset + 2) {
        uint8_t tlv_type = raw[tlv_offset];
        uint8_t tlv_len = raw[tlv_offset + 1];

        if (tlv_type == CFDP_TLV_ENTITY_ID && len >= tlv_offset + 2 + tlv_len) {
            cfdp_view_init(&out->fault_entity_id, raw + tlv_offset + 2, tlv_len);
        }
    }

    return (int)len;
}

// Returns -1 on error, bytes parsed on success
int cfdp_pdu_finished_parse(const uint8_t *raw, size_t len, cfdp_pdu_finished_t *out) {
    if (raw == NULL || out == NULL)
        return -1;

    if (len < 4)
        return -1;

    out->condition_code = (raw[0] >> 4) & 0x0F;
    // 1 bit spare
    out->delivery_code = (raw[0] >> 6) & 0x01;
    out->file_status = (raw[0] >> 7) & 0x03;

    // filestore responses
    cfdp_view_init_empty(&out->filestore_responses);
    uint8_t filestore_responses_type = raw[1];
    uint8_t filestore_responses_len = raw[2];
    if (filestore_responses_type == CFDP_TLV_FILESTORE_REQUEST && len >= 3 + (size_t)filestore_responses_len) {
        cfdp_view_init(&out->fault_entity_id, raw + 3, filestore_responses_len);
    }

    // fault location
    cfdp_view_init_empty(&out->fault_entity_id);
    size_t fault_entity_id_offset = 3 + filestore_responses_len;
    if (!(out->condition_code == CFDP_COND_NOERROR || out->condition_code == CFDP_COND_BAD_CHECKSUM) && len >= fault_entity_id_offset + 2) {
        uint8_t tlv_type = raw[fault_entity_id_offset];
        uint8_t tlv_len = raw[fault_entity_id_offset + 1];

        if (tlv_type == CFDP_TLV_ENTITY_ID && len >= fault_entity_id_offset + 2 + tlv_len) {
            cfdp_view_init(&out->fault_entity_id, raw + fault_entity_id_offset + 2, tlv_len);
        }
    }

    return (int)len;
}

// Returns -1 on error, bytes parsed on success
int cfdp_pdu_ack_parse(const uint8_t *raw, size_t len, cfdp_pdu_ack_t *out) {
    if (raw == NULL || out == NULL)
        return -1;

    if (len < 2)
        return -1;

    out->directive_code = (raw[0] >> 4) & 0x0F;
    out->directive_subtype_code = raw[0] & 0x0F;
    out->directive_subtype_code = (raw[1] >> 4) & 0x0F;
    out->transaction_status = (raw[1]) & 0x03;

    return (int)len;
}

// Returns -1 on error, bytes parsed on success
int cfdp_pdu_segment_request_parse(const uint8_t *raw, size_t len, cfdp_pdu_segment_request_t *out) {
    if (raw == NULL || out == NULL)
        return -1;

    if (len < 8)
        return -1;

    out->start_offset = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16) | ((uint32_t)raw[2] << 8) | raw[3];
    out->end_offset = ((uint32_t)raw[4] << 24) | ((uint32_t)raw[5] << 16) | ((uint32_t)raw[6] << 8) | raw[7];

    return (int)len;
}

// Returns -1 on error, bytes parsed on success
int cfdp_pdu_nak_parse(const uint8_t *raw, size_t len, cfdp_pdu_nak_t *out) {
    if (raw == NULL || out == NULL)
        return -1;

    if (len < 16)
        return -1;

    out->start_of_scope = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16) | ((uint32_t)raw[2] << 8) | raw[3];
    out->end_of_scope = ((uint32_t)raw[4] << 24) | ((uint32_t)raw[5] << 16) | ((uint32_t)raw[6] << 8) | raw[7];

    return (int)len;
}

// Returns -1 on error, bytes parsed on success
int cfdp_pdu_prompt_parse(const uint8_t *raw, size_t len, cfdp_pdu_prompt_t *out) {
    if (raw == NULL || out == NULL)
        return -1;
    if (len < 1)
        return -1;

    out->response_required = (raw[0] >> 7) & 0b1;
    return (int)len;
}

// Returns -1 on error, bytes parsed on success
int cfdp_pdu_keep_alive_parse(const uint8_t *raw, size_t len, cfdp_pdu_keep_alive_t *out) {
    if (raw == NULL || out == NULL)
        return -1;

    if (len < 4)
        return -1;

    out->progress = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16) | ((uint32_t)raw[2] << 8) | raw[3];

    return (int)len;
}

int cfdp_prepare_pdu_header(uint8_t *buff, cfdp_transaction_t *transaction, uint16_t pdu_len, cfdp_pdu_type_t pdu_type) {
    if (buff == NULL || transaction == NULL || pdu_len == 0)
        return -1;

    uint8_t direction = (transaction->direction == CFDP_SEND) ? 1 : 0;
    uint8_t mode = (transaction->reliable_mode) ? 0 : 1;
    uint8_t crc_present = 0;
    uint8_t large_file = 0;

    buff[0] = (0b001 << 5) | ((pdu_type & 0b1) << 4) | (direction << 3) | (mode << 2) | (crc_present << 1) | large_file;

    uint16_to_big_endian(pdu_len, buff + 1);

    uint8_t has_segment_metadata = 0;
    buff[3] = 0b00110011 | (has_segment_metadata << 3);

    uint32_to_big_endian(transaction->transaction_id.entity_id, buff + 4);
    uint32_to_big_endian(transaction->transaction_id.seq_num, buff + 8);
    uint32_to_big_endian(transaction->dest_entity_id, buff + 12);

    return 0;
}

int cfdp_send_metadata(cfdp_transaction_t *transaction) {
    if (transaction == NULL)
        return 0;

    size_t source_filename_length = transaction->source_filename.length;
    size_t dest_filename_length = transaction->dest_filename.length;
    size_t metadata_size = 7 + source_filename_length + dest_filename_length;
    size_t pdu_data_length = metadata_size + 1;

    uint8_t buff[pdu_data_length + 16];
    cfdp_prepare_pdu_header(buff, transaction, (uint16_t)pdu_data_length, CFDP_FILE_DIRECTIVE);

    uint8_t *metadata_buff = buff + 16;

    metadata_buff[0] = CFDP_DIR_METADATA;

    metadata_buff[1] = 0;

    uint8_t req_closure = (transaction->reliable_mode) ? 0 : REQ_CLOSURE;
    metadata_buff[1] |= (req_closure << 6);
    metadata_buff[1] |= CHECKSUM_TYPE;

    uint32_to_big_endian(transaction->file_size, metadata_buff + 2);

    metadata_buff[6] = source_filename_length;
    for (size_t i = 0; i < source_filename_length; i++) {
        metadata_buff[7 + i] = (transaction->source_filename.value)[i];
    }

    metadata_buff[7 + source_filename_length] = (dest_filename_length);
    for (size_t i = 0; i < dest_filename_length; i++) {
        metadata_buff[8 + i + source_filename_length] = (transaction->dest_filename.value)[i];
    }

    cfdp_send(transaction, buff, pdu_data_length + 16);
    transaction->state = CFDP_SEND_STATE_FILE_SEND;
    return 0;
}

int cfdp_send_filedata(cfdp_transaction_t *transaction, uint32_t offset, uint32_t size) {
    if (transaction == NULL)
        return -1;

    uint8_t buff[20 + size];
    cfdp_prepare_pdu_header(buff, transaction, 4 + size, CFDP_FILE_DATA);

    uint8_t *filedata_buff = buff + 16;

    uint32_to_big_endian(offset, filedata_buff);

    memcpy(filedata_buff + 4, transaction->file_data + offset, size);

    cfdp_send(transaction, buff, 20 + size);

    transaction->file_offset += size;
    return size;
}

/**
 * cfdp_send_nak
 *
 * Builds and transmits a NAK PDU listing every segment gap currently in the
 * transaction's nak_buf. Does NOT pop the buffer — gaps stay tracked until the
 * sender retransmits and the caller clears them on receipt.
 *
 * NOTE: this needs to be reworked so that we don't exceed a nak of our transaction frame. For this purpose, NAK should not boock
 */
int cfdp_send_nak(cfdp_transaction_t *transaction) {
    if (transaction == NULL || transaction->nak_buf.size == 0) {
        return -1;
    }

    uint32_t n = transaction->nak_buf.size;
    // NAK payload: 1 (directive code) + 4 (start_of_scope) + 4 (end_of_scope) + 8*n (segment requests)
    size_t nak_data_size = 9 + (8 * n);
    uint8_t buff[16 + nak_data_size];

    cfdp_prepare_pdu_header(buff, transaction, (uint16_t)nak_data_size, CFDP_FILE_DIRECTIVE);

    uint8_t *nak_buff = buff + 16;
    nak_buff[0] = CFDP_DIR_NAK; // directive code

    uint32_t start_scope = transaction->file_size;
    uint32_t end_scope = 0;

    // Iterate the ring buffer without popping — read segments in tail→head order
    for (uint32_t i = 0; i < n; i++) {
        cfdp_pdu_segment_request_t seg = transaction->nak_buf.segments[transaction->nak_buf.tail + i % CFDP_MAX_SEGMENTS];
        uint32_to_big_endian(seg.start_offset, nak_buff + 9 + (i * 8));
        uint32_to_big_endian(seg.end_offset, nak_buff + 13 + (i * 8));
        if (seg.start_offset < start_scope) {
            start_scope = seg.start_offset;
        }
        if (seg.end_offset > end_scope) {
            end_scope = seg.end_offset;
        }
    }

    uint32_to_big_endian(start_scope, nak_buff + 1); // start_of_scope: beginning of file
    uint32_to_big_endian(end_scope, nak_buff + 5);   // end_of_scope: full file extent

    cfdp_send(transaction, buff, 16 + nak_data_size);
    return nak_data_size;
}

int cfdp_send_metadata_nak(cfdp_pdu_header_t *header) {
    if (header == NULL) {
        return -1;
    }

    uint8_t buff[32] = {0};

    uint8_t direction = 0b1;
    uint8_t mode = header->transmission_mode;
    uint8_t crc_present = header->crc;
    uint8_t large_file = header->largefile;

    buff[0] = (0b001 << 5) | (direction << 3) | (mode << 2) | (crc_present << 1) | large_file | 0b0;

    uint16_to_big_endian(16, buff + 1);

    uint8_t has_segment_metadata = header->segment_metadata_field;
    buff[3] = 0b00110011 | (has_segment_metadata << 3); // fix :(

    uint32_to_big_endian(header->source_entity_id, buff + 4);
    uint32_to_big_endian(header->transaction_seq, buff + 8);
    uint32_to_big_endian(header->dest_entity_id, buff + 12);

    return 32;
}

/**
 * cfdp_send_fin
 *
 * Builds and transmits a Finished PDU (directive code 0x05).
 * Used by the receive side in reliable mode to tell the sender that
 * the complete file has been received and the checksum passed.
 *
 * Finished PDU data field (BB Pg. 80-81):
 *   byte 0 : directive code (0x05)
 *   byte 1 : condition_code[7:4] | spare[3] | delivery_code[2] | file_status[1:0]
 *
 * We always send: condition = NO_ERROR, delivery_code = 0 (complete), file_status = 0b00.
 * No filestore-response or fault-location TLVs are appended.
 */
int cfdp_send_fin(cfdp_transaction_t *transaction, uint8_t condition_code) {
    if (transaction == NULL)
        return -1;

    // pdu_data_length: 1 (directive) + 1 (flags byte)
    uint8_t buff[18];
    cfdp_prepare_pdu_header(buff, transaction, 2, CFDP_FILE_DIRECTIVE);

    uint8_t *fin_buff = buff + 16;
    fin_buff[0] = CFDP_DIR_FINISHED;

    uint8_t del_code = (transaction->delivery_complete) ? 0 : 1;
    fin_buff[1] = (condition_code << 4) | (del_code << 2) | (0x0);

    cfdp_send(transaction, buff, 18);
    return 0;
}

int cfdp_send_ack(cfdp_transaction_t *transaction, uint8_t acked_directive_code, uint8_t directive_subtype_code, uint8_t condition_code,
                  uint8_t transaction_status) {
    if (transaction == NULL)
        return -1;

    // pdu_data_length: 1 (directive code) + 2 (ACK parameters)
    uint8_t buff[19];
    cfdp_prepare_pdu_header(buff, transaction, 3, CFDP_FILE_DIRECTIVE);

    uint8_t *ack_buff = buff + 16;
    ack_buff[0] = CFDP_DIR_ACK;
    ack_buff[1] = ((acked_directive_code & 0x0F) << 4) | (directive_subtype_code & 0x0F);
    ack_buff[2] = ((condition_code & 0x0F) << 4) | (transaction_status & 0x03);

    cfdp_send(transaction, buff, 19);
    return 0;
}

int cfdp_send_eof(cfdp_transaction_t *transaction, uint8_t condition_code) {
    size_t fault_location_size = 0;
    if (condition_code != CFDP_COND_NOERROR) {
        fault_location_size = 6;
    }
    // pdu_data_length: 1 (directive) + 1 (condition/spare) + 4 (checksum) + 4 (filesize) + optional fault TLV
    size_t pdu_data_length = 10 + fault_location_size;
    uint8_t buff[16 + pdu_data_length];

    cfdp_prepare_pdu_header(buff, transaction, (uint16_t)pdu_data_length, CFDP_FILE_DIRECTIVE);

    uint8_t *eof_buff = buff + 16;

    // Directive code must be the first byte of the PDU data field (BB Table 5-4, Pg. 78)
    eof_buff[0] = CFDP_DIR_EOF;
    eof_buff[1] = (condition_code & 0xF) << 4;
    uint32_to_big_endian(cfdp_calculate_modular_checksum(transaction), eof_buff + 2);
    uint32_to_big_endian(transaction->file_offset, eof_buff + 6);

    // We're making all entity IDs 4 Bytes, but we still have to encode TLV Format
    if (condition_code != CFDP_COND_NOERROR) {
        eof_buff[10] = 0x06;
        eof_buff[11] = 0x04;
        uint32_to_big_endian(transaction->transaction_id.entity_id, eof_buff + 12);
    }
    cfdp_send(transaction, buff, 16 + pdu_data_length);
    return 0;
}

int cfdp_resend(cfdp_transaction_t *transaction) {
    while (transaction->nak_buf.size > 0) {
        cfdp_pdu_segment_request_t seg = cfdp_nak_buf_pop(&transaction->nak_buf);

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