#include "cfdp_pdu.h"

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

    size_t var_len = out->entity_id_len + seq_len + out->entity_id_len;
    size_t header_len = 4 + var_len;

    if (len < header_len)
        return -1;

    const uint8_t *pos = raw + 4;

    cfdp_view_init(&out->source_entity_id, pos, out->entity_id_len);
    pos += out->entity_id_len;

    cfdp_view_init(&out->transaction_seq, pos, seq_len);
    pos += seq_len;

    cfdp_view_init(&out->dest_entity_id, pos, out->entity_id_len);

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
    if (filestore_responses_type == CFDP_TLV_FILESTORE_REQUEST && len >= 3 + filestore_responses_len) {
        cfdp_view_init(&out->fault_entity_id, raw + 3, filestore_responses_len);
    }

    // fault location
    cfdp_view_init_empty(&out->fault_entity_id);
    size_t fault_entity_id_offset = 3 + filestore_responses_len;
    if ((out->condition_code != CFDP_COND_NOERROR || out->condition_code != CFDP_COND_BAD_CHECKSUM) && len >= fault_entity_id_offset + 2) {
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
    // 2 spare bits
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

    size_t segment_request_count = (len - 8) / 8;
    out->segment_request_count = segment_request_count;

    for (size_t i = 0; i < segment_request_count; i++) {
        cfdp_pdu_segment_request_parse(raw + 8 + (i * 8), 8, out->segment_requests + i);
    }
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
