#ifndef RADIO_CFDP
#define RADIO_CFDP

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * NOTE: For brevity, sources in comments are abbreviated as follows.
 * 1. CFDP Green Book abbreviated to GB.
 * 2. CFDP Blue Book abbreviated to BB.
 *
 * References to official CCSDS documentation should be cited with page number.
 * Please use these conventions when amending these files.
 */

// Constants:
// Version number, always 001, see CFDP Blue Book 727.0-B-5 (Table 5-1, Pg. 75)
#define CFDP_VERSION_NUMBER 0b001

// For Entity ID field of EOF PDU. Perhaps unneeded.
#define ENTITY_ID_SPACECRAFT 0x01
#define ENTITY_ID_GROUND 0x02

// Currently Supported PDU types
#define CFDP_PDU_TYPE_DIRECTIVE 0
#define CFDP_PDU_TYPE_FILEDATA  1

// Directive codes (Table 5-4, Pg. 78)
#define CFDP_DIR_EOF      0x04
#define CFDP_DIR_FINISHED 0x05
#define CFDP_DIR_ACK      0x06
#define CFDP_DIR_METADATA 0x07
#define CFDP_DIR_NAK      0x08
#define CFDP_DIR_PROMPT   0x09
#define CFDP_DIR_KEEPALIVE 0x0C

// Condition Codes (Table 5-5, Pg. 79)
// No error: Always zero
#define CFDP_COND_NOERROR 0
// Positive ACK limit reached
#define CFDP_COND_ACK_LIMIT 0b0001
// Keep alive limit reached
#define CFDP_COND_KEEPALIVE_LIMIT 0b0010
// Invalid transmission mode
#define CFDP_COND_INVALID_TRANSMISSION 0b0011
// Filestore rejection -> UNUSED
#define CFDP_COND_FILESTORE_REJECT 0b0100
// File checksum failure
#define CFDP_COND_FILE_CHECKSUM_FAIL 0b0101
// File size error
#define CFDP_COND_FILE_SIZEERROR 0b0110
// NAK limit reached
#define CFDP_COND_NAK_LIMIT 0b0111
// Inactivity detected
#define CFDP_COND_INACTIVITY 0b1000
// Invalid file structure
#define CFDP_COND_FILE_STRUCTURE_INVALID 0b1001
// Check limit reached
#define CFDP_COND_CHECK_LIMIT 0b1010
// Unsupported checksum Type
#define CFDP_COND_BAD_CHECKSUM 0b1011
// Suspend.request received -> UNUSED
#define CFDP_COND_SUSPEND_REQ 0b1110
// Cancel.request received -> UNUSED
#define CFDP_COND_CANCEL_REQ 0b1111

// TLV Types (BB pg. 86)
#define CFDP_TLV_FILESTORE_REQUEST 0x00
#define CFDP_TLV_FILESTORE_RESPONSE 0x01
#define CFDP_TLV_MESSAGE_TO_USER 0x02
#define CFDP_TLV_FAULT_HANDLER 0x04
#define CFDP_TLV_FLOW_LABEL 0x05
#define CFDP_TLV_ENTITY_ID 0x06

/*
 * CFDP generic variable length field struct
 */
typedef struct cfdp_data_view {
    uint8_t len;
    const uint8_t *data;
} cfdp_data_view_t;

typedef struct cfdp_transaction_id {
    uint64_t entity_id;
    uint64_t seq_num;
} cfdp_transaction_id_t;

/*
 * CFDP PDU header ; BB Pg. 75
 */
typedef struct cfdp_pdu_header {
    uint8_t version_number : 3;
    uint8_t pdu_type : 1;
    uint8_t direction : 1;
    uint8_t transmission_mode : 1;
    uint8_t crc : 1;
    uint8_t largefile : 1;
    uint16_t pdu_data_length;
    uint8_t segmentation_control : 1;
    uint8_t entity_id_len : 3;
    uint8_t segment_metadata_field : 1;
    cfdp_data_view_t source_entity_id;
    cfdp_data_view_t transaction_seq;
    cfdp_data_view_t dest_entity_id;
} cfdp_pdu_header_t;

/*
 * CFDP metadata pdu structure ; BB Pg.83
 * Since we don't use a filesystem, source_id and destination_id
 * are just treated as indices
 *
 * TODO: Change this?
 */
typedef struct cfdp_pdu_metadata {
    uint8_t : 1; // Reserved
    uint8_t closure_req : 1;
    uint8_t : 2; // Reserved
    uint8_t checksum_type : 4;
    uint32_t file_length;
    uint8_t source_id;
    uint8_t dest_id;
    cfdp_data_view_t options;
} cfdp_pdu_metadata_t;

/*
 * CFDP file data PDU structure; BB Pg. 85
 */
typedef struct cfdp_pdu_filedata {
    uint8_t continue_state : 2;
    uint8_t segment_metadata_length : 6;
    cfdp_data_view_t segment_metadata;
    uint32_t offset;
    cfdp_data_view_t data;
} cfdp_pdu_filedata_t;

/*
 * CFDP EOF PDU structure; BB Pg. 80
 */
typedef struct cfdp_pdu_eof {
    uint8_t condition_code : 4;
    uint8_t : 4;
    uint32_t checksum;
    uint32_t filesize;
    cfdp_data_view_t fault_entity_id;
} cfdp_pdu_eof_t;

/*
 * Format for parsing TLV objects
 */
typedef struct cfdp_tlv {
    uint8_t type;
    uint8_t length;
    const uint8_t *value;
} cfdp_tlv_t;

/*
* Format for parsing LV objects
* NOTE: CFDP BB separates the notion of "LV" from "Variable".
* I don't see a clear reasoning for this, so I may change
* cfdp_data_view_t to cfdp_lv_t broadly.
*/
typedef struct cfdp_lv {
    uint8_t length;
    const uint8_t *value;
} cfdp_lv_t;

static inline void cfdp_view_init(cfdp_data_view_t *view, const uint8_t *data, uint8_t len) {
    view->data = data;
    view->len = len;
}

static inline void cfdp_view_init_empty(cfdp_data_view_t *view) {
    view->data = NULL;
    view->len = 0;
}

static inline uint64_t cfdp_view_to_uint(const cfdp_data_view_t *view) {
    uint64_t result = 0;
    for (uint8_t i = 0; i < view->len && i < 8; i++) {
        result = (result << 8) | view->data[i];
    }
    return result;
}

static inline void cfdp_transaction_id_init(const cfdp_pdu_header_t *header, cfdp_transaction_id_t *out) {
    out->entity_id = cfdp_view_to_uint(&header->source_entity_id);
    out->seq_num = cfdp_view_to_uint(&header->transaction_seq);
}

static inline bool cfdp_tlv_next(const uint8_t **pos, const uint8_t *end, cfdp_tlv_t *out) {
    if (*pos + 2 > end)
        return false;

    out->type = (*pos)[0];
    out->length = (*pos)[1];
    out->value = *pos + 2;

    if (out->value + out->length > end)
        return false;

    *pos = out->value + out->length;
    return true;
}

void cfdp_data_view_clear_data(cfdp_data_view_t *view);

int cfdp_pdu_header_parse(const uint8_t *raw, size_t len, cfdp_pdu_header_t *out);

int cfdp_pdu_metadata_parse(const uint8_t *raw, size_t len, cfdp_pdu_metadata_t *out);

int cfdp_pdu_filedata_parse(const uint8_t *raw, size_t len, bool large_file, bool has_segment_metadata, cfdp_pdu_filedata_t *out);

int cfdp_pdu_eof_parse(const uint8_t *raw, size_t len, bool large_file, cfdp_pdu_eof_t *out);

#endif
