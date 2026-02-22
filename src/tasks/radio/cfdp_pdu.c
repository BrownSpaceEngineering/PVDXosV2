#include "cfdp_pdu.h"

void cfdp_data_view_clear_data(cfdp_data_view_t *view) {
    view->data = NULL;
    view->len = 0;
}

// Returns -1 on error, bytes parsed on success
int cfdp_pdu_header_parse(const uint8_t *raw,
                          size_t len,
                          cfdp_pdu_header_t *out) {
    if (raw == NULL || out == NULL) return -1;
    if (len < 4) return -1;

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

    if (len < header_len) return -1;

    const uint8_t *pos = raw + 4;

    cfdp_view_init(&out->source_entity_id, pos, out->entity_id_len);
    pos += out->entity_id_len;

    cfdp_view_init(&out->transaction_seq, pos, seq_len);
    pos += seq_len;

    cfdp_view_init(&out->dest_entity_id, pos, out->entity_id_len);

    return (int)header_len;
}

// Returns -1 on error, bytes parsed on success
int cfdp_pdu_metadata_parse(const uint8_t *raw,
                            size_t len,
                            cfdp_pdu_metadata_t *out) {
    if (raw == NULL || out == NULL) return -1;
    if (len < 7) return -1;

    out->closure_req = (raw[0] >> 6) & 0x01;
    out->checksum_type = raw[0] & 0x0F;

    out->file_length = ((uint32_t)raw[1] << 24) |
                       ((uint32_t)raw[2] << 16) |
                       ((uint32_t)raw[3] << 8) |
                       raw[4];

    uint8_t src_len = raw[5];
    if (len < 6 + src_len + 1) return -1;
    out->source_id = (src_len > 0) ? raw[6] : 0;

    size_t dest_offset = 6 + src_len;
    uint8_t dest_len = raw[dest_offset];
    if (len < dest_offset + 1 + dest_len) return -1;
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
int cfdp_pdu_filedata_parse(const uint8_t *raw,
                            size_t len,
                            bool large_file,
                            bool has_segment_metadata,
                            cfdp_pdu_filedata_t *out) {
    if (raw == NULL || out == NULL) return -1;

    size_t offset_size = large_file ? 8 : 4;
    const uint8_t *pos = raw;

    out->continue_state = 0;
    out->segment_metadata_length = 0;
    cfdp_view_init_empty(&out->segment_metadata);

    if (has_segment_metadata) {
        if (len < 1) return -1;

        out->continue_state = (pos[0] >> 6) & 0x03;
        out->segment_metadata_length = pos[0] & 0x3F;
        pos++;

        if (len < 1 + out->segment_metadata_length + offset_size) return -1;

        cfdp_view_init(&out->segment_metadata, pos, out->segment_metadata_length);
        pos += out->segment_metadata_length;
    } else {
        if (len < offset_size) return -1;
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
int cfdp_pdu_eof_parse(const uint8_t *raw,
                       size_t len,
                       bool large_file,
                       cfdp_pdu_eof_t *out) {
    if (raw == NULL || out == NULL) return -1;

    size_t filesize_len = large_file ? 8 : 4;
    size_t min_len = 1 + 4 + filesize_len;

    if (len < min_len) return -1;

    out->condition_code = (raw[0] >> 4) & 0x0F;

    out->checksum = ((uint32_t)raw[1] << 24) |
                    ((uint32_t)raw[2] << 16) |
                    ((uint32_t)raw[3] << 8) |
                    raw[4];

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
