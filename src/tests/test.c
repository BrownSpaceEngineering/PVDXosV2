
#include "tests/test.h"

#include <math.h>
#include <string.h>

#include "ccsds/spp.h"
#include "ccsds/uslp.h"
#include "cfdp/cfdp_pdu.h"
#include "cfdp/cfdp_task.h"
#include "cfdp/cfdp_utils.h"
#include "logging.h"
#include "tests/test_pmb.h"

int tests_passed = 0;
int tests_total = 0;

#if defined(UNITTEST)
uint8_t test_mem[512];
#endif

void test_spp(void);
void test_matrix_product(void);
void test_cfdp(void);
void test_uslp(void);
void test_float(void);

void tests_run(void) {
#ifdef TEST_SPP
    test_spp();
#endif
#ifdef TEST_CFDP
    test_cfdp();
#endif
#ifdef TEST_USLP
    test_uslp();
#endif
#ifdef TEST_FLOAT
    test_float();
#endif
    test_pmb();
    test_log("test results: %d/%d passed", tests_passed, tests_total);
}

// #ifdef UNITTEST
#ifdef TEST_SPP
void test_spp(void) {
    spp_packet_t packet = spp_packet_create_header_only(0xBB, 0, 1, 0b10, 0b0011111111111111, 0xAA);
    test_log("----- testing spp -----\n");
    test_log("spp packet test:\n");
    test_log("header size: %d\n", sizeof(packet.header));
    test_log("full packet size (max w/ internal buffer): %d\n", sizeof(packet));

    test_log("apid: %x\n", packet.header.application_process_id);
    PVDX_ASSERT_MSG(packet.header.application_process_id == 0xBB, "apid\n");

    test_log("secondary_header_flag: %x\n", packet.header.secondary_header_flag);
    PVDX_ASSERT_MSG(packet.header.secondary_header_flag == 0, "secondary_header_flag\n");

    test_log("packet_type: %x\n", packet.header.packet_type);
    PVDX_ASSERT_MSG(packet.header.packet_type == 1, "packet_type\n");

    test_log("sequence_flags: %x\n", packet.header.sequence_flags);
    PVDX_ASSERT_MSG(packet.header.sequence_flags == 0b10, "sequence_flags\n");

    test_log("packet_seq_count_or_name: %x\n", packet.header.sequence_count);
    PVDX_ASSERT_MSG(packet.header.sequence_count == 0b0011111111111111, "packet_seq_count_or_name\n");

    test_log("data_length: %x\n", packet.header.data_length);
    PVDX_ASSERT_MSG(packet.header.data_length == 0xAA, "data_length");
}
#endif // TEST_SPP

#if defined(UNITTEST) && defined(TEST_CFDP)
void test_cfdp(void) {
    test_log("----- testing cfdp -----\n");

    test_log("cfdp header parse test:\n");
    uint8_t raw_header[] = {0x20, 0x00, 0x0A, 0x00, 0x01, 0x05, 0x02};
    cfdp_pdu_header_t header;
    int ret = cfdp_pdu_header_parse(raw_header, sizeof(raw_header), &header);

    test_log("return value: %d\n", ret);
    PVDX_ASSERT(ret == 7 && "header parse return");

    test_log("version_number: %d\n", header.version_number);
    PVDX_ASSERT(header.version_number == CFDP_VERSION_NUMBER && "version_number");

    test_log("pdu_type: %d\n", header.pdu_type);
    PVDX_ASSERT(header.pdu_type == CFDP_PDU_TYPE_DIRECTIVE && "pdu_type");

    test_log("pdu_data_length: %d\n", header.pdu_data_length);
    PVDX_ASSERT(header.pdu_data_length == 10 && "pdu_data_length");

    test_log("entity_id_len: %d\n", header.entity_id_len);
    PVDX_ASSERT(header.entity_id_len == 1 && "entity_id_len");

    test_log("cfdp header parse error (too short) test:\n");
    ret = cfdp_pdu_header_parse(raw_header, 3, &header);
    PVDX_ASSERT(ret == -1 && "header parse too short");

    test_log("cfdp metadata parse test:\n");
    uint8_t raw_metadata[] = {0x40, 0x00, 0x00, 0x01, 0x00, 0x01, 0x0A, 0x01, 0x0B};
    cfdp_pdu_metadata_t metadata;
    ret = cfdp_pdu_metadata_parse(raw_metadata, sizeof(raw_metadata), &metadata);

    test_log("return value: %d\n", ret);
    PVDX_ASSERT(ret == 9 && "metadata parse return");

    test_log("closure_req: %d\n", metadata.closure_req);
    PVDX_ASSERT(metadata.closure_req == 1 && "closure_req");

    test_log("checksum_type: %d\n", metadata.checksum_type);
    PVDX_ASSERT(metadata.checksum_type == 0 && "checksum_type");

    test_log("file_length: %u\n", metadata.file_length);
    PVDX_ASSERT(metadata.file_length == 256 && "file_length");

    test_log("source_id: 0x%02x\n", metadata.source_id);
    PVDX_ASSERT(metadata.source_id == 0x0A && "source_id");

    test_log("dest_id: 0x%02x\n", metadata.dest_id);
    PVDX_ASSERT(metadata.dest_id == 0x0B && "dest_id");

    test_log("options.len: %d\n", metadata.options.len);
    PVDX_ASSERT(metadata.options.len == 0 && "metadata options empty");

    test_log("cfdp eof parse test:\n");
    uint8_t raw_eof[] = {0x00, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x01, 0x00};
    cfdp_pdu_eof_t eof;
    ret = cfdp_pdu_eof_parse(raw_eof, sizeof(raw_eof), false, &eof);

    test_log("return value: %d\n", ret);
    PVDX_ASSERT(ret == 9 && "eof parse return");

    test_log("condition_code: %d\n", eof.condition_code);
    PVDX_ASSERT(eof.condition_code == CFDP_COND_NOERROR && "condition_code");

    test_log("checksum: 0x%08x\n", eof.checksum);
    PVDX_ASSERT(eof.checksum == 0xDEADBEEF && "checksum");

    test_log("filesize: %u\n", eof.filesize);
    PVDX_ASSERT(eof.filesize == 256 && "filesize");

    test_log("fault_entity_id.len: %d\n", eof.fault_entity_id.len);
    PVDX_ASSERT(eof.fault_entity_id.len == 0 && "fault_entity_id empty");

    test_log("cfdp filedata parse test:\n");
    uint8_t raw_filedata[] = {0x00, 0x00, 0x04, 0x00, 0xCA, 0xFE, 0xBA, 0xBE};
    cfdp_pdu_filedata_t filedata;
    ret = cfdp_pdu_filedata_parse(raw_filedata, sizeof(raw_filedata), false, false, &filedata);

    test_log("return value: %d\n", ret);
    PVDX_ASSERT(ret == 8 && "filedata parse return");

    test_log("offset: %u\n", filedata.offset);
    PVDX_ASSERT(filedata.offset == 1024 && "offset");

    test_log("data.len: %d\n", filedata.data.len);
    PVDX_ASSERT(filedata.data.len == 4 && "data length");

    test_log("data[0]: 0x%02x\n", filedata.data.data[0]);
    PVDX_ASSERT(filedata.data.data[0] == 0xCA && "data[0]");

    test_log("data[1]: 0x%02x\n", filedata.data.data[1]);
    PVDX_ASSERT(filedata.data.data[1] == 0xFE && "data[1]");

    // finished no error
    test_log("cfdp fin no error parse test:\n");
    uint8_t raw_fin[] = {0x03, 0x01, 0x00};
    cfdp_pdu_finished_t fin;
    ret = cfdp_pdu_finished_parse(raw_fin, sizeof(raw_fin), &fin);

    test_log("return value: %d\n", ret);
    PVDX_ASSERT(ret == 3 && "finished parse return");

    test_log("cfdp fin condition code: %u\n", fin.condition_code);
    PVDX_ASSERT(fin.condition_code == CFDP_COND_NOERROR && "finished condition code");

    test_log("cfdp fin delivery code: %u\n", fin.delivery_code);
    PVDX_ASSERT(fin.delivery_code == 0 && "finished delivery code");

    test_log("cfdp fin file status: %u\n", fin.file_status);
    PVDX_ASSERT(fin.file_status == 0b11 && "finished file status"); // status unreported

    test_log("cfdp fin filestore response len: %u\n", fin.filestore_responses.len);
    PVDX_ASSERT(fin.filestore_responses.len == 0 && "finished filestore resp len");

    // finished error
    test_log("cfdp fin error parse test:\n");

    uint8_t raw_fin_err[] = {0x37, 0x01, 0x00, 0x06, 0x01, 0x44};
    ret = cfdp_pdu_finished_parse(raw_fin_err, sizeof(raw_fin_err), &fin);

    test_log("return value: %d\n", ret);
    PVDX_ASSERT(ret == 6 && "finished parse return");

    test_log("cfdp fin condition code: %u\n", fin.condition_code);
    PVDX_ASSERT(fin.condition_code == CFDP_COND_INVALID_TRANSMISSION && "finished condition code");

    test_log("cfdp fin delivery code: %u\n", fin.delivery_code);
    PVDX_ASSERT(fin.delivery_code == 1 && "finished delivery code");

    test_log("cfdp fin file status: %u\n", fin.file_status);
    PVDX_ASSERT(fin.file_status == 0b11 && "finished file status"); // status unreported

    test_log("cfdp fin filestore response len: %u\n", fin.filestore_responses.len);
    PVDX_ASSERT(fin.filestore_responses.len == 0 && "finished filestore resp len");

    test_log("cfdp fin fault location length: %u\n", fin.fault_entity_id.len);
    PVDX_ASSERT(fin.fault_entity_id.len == 1 && "finished fault location len");

    test_log("cfdp fin fault location data: %u\n", fin.fault_entity_id.data);
    PVDX_ASSERT(*fin.fault_entity_id.data == 0x44 && "finished fault location data");

    // ack PDU
    test_log("cfdp ack parse test:\n");

    uint8_t raw_ack[] = {0x51, 0x0D};
    cfdp_pdu_ack_t ack;
    ret = cfdp_pdu_ack_parse(raw_ack, sizeof(raw_ack), &ack);

    test_log("return value: %d\n", ret);
    PVDX_ASSERT_MSG(ret == 2, "ack condition code");

    test_log("cfdp ack dir code of acked pdu: %u\n", ack.directive_code);
    PVDX_ASSERT_MSG(ack.directive_code == CFDP_DIR_FINISHED, "ack directive code");

    test_log("cfdp ack directive subtype code: %u\n", ack.directive_subtype_code);
    PVDX_ASSERT_MSG(ack.directive_subtype_code == 0b0001, "ack directive subtype code");

    test_log("cfdp ack condition code: %u\n", ack.condition_code);
    PVDX_ASSERT_MSG(ack.condition_code == CFDP_COND_NOERROR, "ack condition code");

    test_log("cfdp ack transaction status: %u\n", ack.transaction_status);
    PVDX_ASSERT_MSG(ack.transaction_status == 0b01, "ack transaction status");

    // nak PDU
    test_log("cfdp nak parse test:\n");

    uint8_t raw_nak[] = {0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x03, 0x04};
    cfdp_pdu_nak_t nak;
    ret = cfdp_pdu_nak_parse(raw_nak, sizeof(raw_nak), &nak);

    test_log("return value: %d\n", ret);
    PVDX_ASSERT_MSG(ret == 8, "nak parse return");

    test_log("cfdp nak start offset: %u\n", nak.start_of_scope);
    PVDX_ASSERT_MSG(nak.start_of_scope == 0x00000001, "nak start offset");

    test_log("cfdp nak end offset: %u\n", nak.end_of_scope);
    PVDX_ASSERT_MSG(nak.end_of_scope == 0x01020304, "nak end offset");

    // segment request PDU
    test_log("cfdp segment request parse test:\n");

    uint8_t raw_seg_req[] = {0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x03, 0x04};
    cfdp_pdu_segment_request_t seg_req;
    ret = cfdp_pdu_segment_request_parse(raw_seg_req, sizeof(raw_seg_req), &seg_req);

    test_log("return value: %d\n", ret);
    PVDX_ASSERT_MSG(ret == 8, "segment request parse return");

    test_log("cfdp segment request start offset: %u\n", seg_req.start_offset);
    PVDX_ASSERT_MSG(seg_req.start_offset == 0x00000001, "segment request start offset");

    test_log("cfdp segment request end offset: %u\n", seg_req.end_offset);
    PVDX_ASSERT_MSG(seg_req.end_offset == 0x01020304, "segment request end offset");

    // Prepare Header Test
    test_log("cfdp prepare header test:\n");

    cfdp_transaction_t txn = {.checksum_type = 15,
                              .condition_code = CFDP_COND_NOERROR,
                              .delivery_complete = false,
                              .dest_entity_id = 0x1111,
                              .transaction_id = {.entity_id = 0x2222, .seq_num = 0x1},
                              .direction = CFDP_SEND,
                              .file_offset = 0,
                              .reliable_mode = true,
                              .type = IMAGE,
                              .state = CFDP_SEND_STATE_METADATA_SEND};

    uint8_t buff[16];

    ret = cfdp_prepare_pdu_header(buff, &txn, 266, CFDP_FILE_DIRECTIVE);
    test_log("return value: %d\n", ret);
    PVDX_ASSERT_MSG(ret == 16, "prepare header return");

    cfdp_pdu_header_parse(buff, sizeof(buff), &header);

    test_log("version_number: %d\n", header.version_number);
    PVDX_ASSERT(header.version_number == CFDP_VERSION_NUMBER && "version_number");

    test_log("pdu_type: %d\n", header.pdu_type);
    PVDX_ASSERT(header.pdu_type == CFDP_PDU_TYPE_DIRECTIVE && "pdu_type");

    test_log("pdu_data_length: %d\n", header.pdu_data_length);
    PVDX_ASSERT(header.pdu_data_length == 266 && "pdu_data_length");

    test_log("entity_id_len: %d\n", header.entity_id_len);
    PVDX_ASSERT(header.entity_id_len == 4 && "entity_id_len");

    test_log("dest_entity_id: %d\n", header.dest_entity_id);
    PVDX_ASSERT(header.dest_entity_id == 0x1111 && "dest_entity_id");

    test_log("source_entity_id: %d\n", header.source_entity_id);
    PVDX_ASSERT(header.source_entity_id == 0x2222 && "source_entity_id");

    test_log("transaction_seq: %d\n", header.transaction_seq);
    PVDX_ASSERT(header.transaction_seq == 0x1 && "transaction_seq");

    // ------- CFDP SEND TESTS ---------

    // Send Metadata Test
    txn = (cfdp_transaction_t){0};

    txn.file_size = 0xDEADBEEF;
    txn.state = CFDP_SEND_STATE_METADATA_SEND;
    txn.reliable_mode = true;
    txn.checksum_type = 0;

    ret = cfdp_send_metadata(&txn);

    test_log("metadata return: %d\n", ret);
    PVDX_ASSERT(ret == 0 && "metadata_return");

    uint8_t *md_raw = &test_mem[17];

    test_log("metadata byte 1: %x\n", md_raw[0]);
    PVDX_ASSERT(md_raw[0] == 0x00 && "closure + checksum type");

    test_log("metadata filesize: b1: %x b2: %x b3: %x b4: %x (Big Endian)\n", md_raw[1], md_raw[2], md_raw[3], md_raw[4]);
    PVDX_ASSERT(md_raw[1] == 0xDE && md_raw[2] == 0xAD && md_raw[3] == 0xBE && md_raw[4] == 0xEF && "file size");

    // Send Filedata/EOF Test
    txn = (cfdp_transaction_t){0};
    txn.file_size = 0x01;
    txn.condition_code = 0; // No ERROR
    uint8_t data = 0xCC;
    txn.file_data = &data;
    txn.checksum_type = 15;
    txn.source_filename = (cfdp_lv_t){.length = 0, .value = NULL};
    txn.dest_filename = (cfdp_lv_t){.length = 0, .value = NULL};

    // Filedata
    ret = cfdp_send_filedata(&txn, 0, 1);

    test_log("filedata return: %d\n", ret);
    PVDX_ASSERT(ret == 1 && "filedata return");

    uint8_t *fd_raw = &test_mem[16];

    test_log("fd offset: b1: %x b2: %x b3: %x b4: %x (Big Endian)\n", fd_raw[0], fd_raw[1], fd_raw[2], fd_raw[3]);
    PVDX_ASSERT(fd_raw[0] == 0x00 && fd_raw[1] == 0x00 && fd_raw[2] == 0x00 && fd_raw[3] == 0x00 && "offset");

    test_log("fd data: %x\n", fd_raw[4]);
    PVDX_ASSERT(fd_raw[4] == 0xCC && "file_data");

    txn.file_offset = 1;

    // EOF
    ret = cfdp_send_eof(&txn);

    test_log("eof return: %d\n", ret);
    PVDX_ASSERT(ret == 0 && "eof_return");

    uint8_t *eof_raw = &test_mem[17];

    test_log("eof byte 1: %x\n", md_raw[0]);
    PVDX_ASSERT(eof_raw[0] == 0 && "condition code");

    test_log("eof checksum: b1: %x b2: %x b3: %x b4: %x (Big Endian)\n", eof_raw[1], eof_raw[2], eof_raw[3], eof_raw[4]);
    PVDX_ASSERT(eof_raw[1] == 0xCC && eof_raw[2] == 0x00 && eof_raw[3] == 0x00 && eof_raw[4] == 0x00 && "checksum");

    test_log("eof file size: b1: %x b2: %x b3: %x b4: %x (Big Endian)\n", eof_raw[5], eof_raw[6], eof_raw[7], eof_raw[8]);
    PVDX_ASSERT(eof_raw[5] == 0x00 && eof_raw[6] == 0x00 && eof_raw[7] == 0x00 && eof_raw[8] == 0x01 && "file_size");

    // Send FIN Test (delivery complete, no error)
    txn = (cfdp_transaction_t){0};

    txn.reliable_mode = true;
    txn.condition_code = CFDP_COND_NOERROR;
    txn.delivery_complete = true;

    ret = cfdp_send_fin(&txn);

    test_log("fin return: %d\n", ret);
    PVDX_ASSERT(ret == 0 && "fin_return");

    test_log("fin directive code: %x\n", test_mem[16]);
    PVDX_ASSERT(test_mem[16] == 0x05 && "fin directive code");

    uint8_t *fin_raw = &test_mem[17];

    test_log("fin flags byte: %x\n", fin_raw[0]);
    // condition code (4b) | spare (1b) | delivery code (1b) | file status (2b, hardcoded to 0)
    PVDX_ASSERT(fin_raw[0] == 0x00 && "no error + data complete + file status 0");

    test_log("fin filestore response TLV: type: %x length: %x\n", fin_raw[1], fin_raw[2]);
    PVDX_ASSERT(fin_raw[1] == CFDP_TLV_FILESTORE_RESPONSE && fin_raw[2] == 0x00 && "empty filestore response TLV");

    // Send FIN with error / incomplete delivery
    txn.condition_code = 5; // File checksum failure
    txn.delivery_complete = false;

    ret = cfdp_send_fin(&txn);

    test_log("fin (error) return: %d\n", ret);
    PVDX_ASSERT(ret == 0 && "fin_error_return");

    test_log("fin (error) directive code: %x\n", test_mem[16]);
    PVDX_ASSERT(test_mem[16] == 0x05 && "fin (error) directive code");

    test_log("fin (error) flags byte: %x\n", fin_raw[0]);
    PVDX_ASSERT(fin_raw[0] == 0x54 && "checksum failure + data incomplete + file status 0");

    test_log("fin (error) filestore response TLV: type: %x length: %x\n", fin_raw[1], fin_raw[2]);
    PVDX_ASSERT(fin_raw[1] == CFDP_TLV_FILESTORE_RESPONSE && fin_raw[2] == 0x00 && "empty filestore response TLV");

    test_log("fin (error) fault location TLV: type: %x length: %x\n", fin_raw[3], fin_raw[4]);
    PVDX_ASSERT(fin_raw[3] == CFDP_TLV_ENTITY_ID && fin_raw[4] == 0x04 && "entity id TLV header");

    test_log("fin (error) fault entity id: b1: %x b2: %x b3: %x b4: %x (Big Endian)\n", fin_raw[5], fin_raw[6], fin_raw[7], fin_raw[8]);
    PVDX_ASSERT(fin_raw[5] == ((ENTITY_ID_SPACECRAFT >> 24) & 0xFF) && fin_raw[6] == ((ENTITY_ID_SPACECRAFT >> 16) & 0xFF) &&
                fin_raw[7] == ((ENTITY_ID_SPACECRAFT >> 8) & 0xFF) && fin_raw[8] == (ENTITY_ID_SPACECRAFT & 0xFF) &&
                "fault location entity id");

    // Send ACK (of EOF) Test
    txn = (cfdp_transaction_t){0};

    txn.reliable_mode = true;
    txn.direction = CFDP_RECV; // receiver acknowledging the sender's EOF

    ret = cfdp_send_ack(&txn, 0x04 /* EOF */, 0 /* subtype */, 0 /* No error */, 2 /* terminated */);

    test_log("ack return: %d\n", ret);
    PVDX_ASSERT(ret == 0 && "ack_return");

    test_log("ack directive code: %x\n", test_mem[16]);
    PVDX_ASSERT(test_mem[16] == 0x06 && "ack directive code");

    uint8_t *ack_raw = &test_mem[17];

    test_log("ack byte 1: %x\n", ack_raw[0]);
    PVDX_ASSERT(ack_raw[0] == 0x40 && "acked directive code (EOF) + subtype");

    test_log("ack byte 2: %x\n", ack_raw[1]);
    PVDX_ASSERT(ack_raw[1] == 0x02 && "condition code + transaction status (terminated)");

    // Send ACK (of FIN) Test
    txn.direction = CFDP_SEND; // sender acknowledging the receiver's FIN

    ret = cfdp_send_ack(&txn, 0x05 /* FIN */, 1 /* subtype */, 0 /* No error */, 3 /* unrecognized */);

    test_log("ack (fin) return: %d\n", ret);
    PVDX_ASSERT(ret == 0 && "ack_fin_return");

    test_log("ack (fin) directive code: %x\n", test_mem[16]);
    PVDX_ASSERT(test_mem[16] == 0x06 && "ack (fin) directive code");

    test_log("ack (fin) byte 1: %x byte 2: %x\n", ack_raw[0], ack_raw[1]);
    PVDX_ASSERT(ack_raw[0] == 0x51 && ack_raw[1] == 0x03 && "FIN ack: directive/subtype + transaction status");

    // Send Stale ACK Test (acknowledges a FIN for a transaction that has already been cleared)
    cfdp_pdu_header_t stale_header = {0};
    stale_header.version_number = 1;
    stale_header.pdu_type = 0; // File directive
    stale_header.direction = 1;
    stale_header.transmission_mode = 0; // Acknowledged
    stale_header.crc = 0;
    stale_header.largefile = 0;
    stale_header.entity_id_len = 3; // 4 byte IDs
    stale_header.source_entity_id = 0x0A0B0C0D;
    stale_header.transaction_seq = 0x01020304;
    stale_header.dest_entity_id = 0x00000042;

    ret = cfdp_send_stale_ack(&stale_header, 0x00);

    test_log("stale ack return: %d\n", ret);
    PVDX_ASSERT(ret == 0 && "stale_ack_return");

    test_log("stale ack directive code: %x\n", test_mem[16]);
    PVDX_ASSERT(test_mem[16] == 0x06 && "stale ack directive code");

    test_log("stale ack byte 1: %x byte 2: %x\n", ack_raw[0], ack_raw[1]);
    PVDX_ASSERT(ack_raw[0] == 0x51 && ack_raw[1] == 0x02 && "acked directive (FIN) + subtype + no error + terminated");

    // Stale ACK with a condition code
    ret = cfdp_send_stale_ack(&stale_header, 0x05);

    test_log("stale ack (cc) return: %d\n", ret);
    PVDX_ASSERT(ret == 0 && "stale_ack_cc_return");

    test_log("stale ack (cc) byte 1: %x byte 2: %x\n", ack_raw[0], ack_raw[1]);
    PVDX_ASSERT(ack_raw[0] == 0x51 && ack_raw[1] == 0x52 && "FIN ack + condition code 5 + terminated");

    // Stale ACK with NULL header
    ret = cfdp_send_stale_ack(NULL, 0x00);

    test_log("stale ack (null) return: %d\n", ret);
    PVDX_ASSERT(ret == -1 && "stale_ack_null_return");

    // Send Metadata NAK Test
    cfdp_pdu_header_t md_nak_header = stale_header; // same values, direction is set by the function

    ret = cfdp_send_metadata_nak(&md_nak_header);

    test_log("metadata nak return: %d\n", ret);
    PVDX_ASSERT(ret == 33 && "metadata_nak_return (bytes sent)");

    test_log("metadata nak directive code: %x\n", test_mem[16]);
    PVDX_ASSERT(test_mem[16] == 0x08 && "metadata nak directive code");

    uint8_t *mnak_raw = &test_mem[17];

    test_log("metadata nak body (all 15 bytes should be zero)\n");
    for (int i = 0; i < 16; i++) {
        PVDX_ASSERT(mnak_raw[i] == 0x00 && "metadata nak body zeroed");
    }

    // Metadata NAK with NULL header
    ret = cfdp_send_metadata_nak(NULL);

    test_log("metadata nak (null) return: %d\n", ret);
    PVDX_ASSERT(ret == -1 && "metadata_nak_null_return");
}
#endif // UNITTEST && TEST_CFDP

#ifdef TEST_USLP
void test_uslp(void) {
    test_log("----- testing uslp -----\n");

    // All frames below have a valid FECF, so each rejection test fails for the reason it names
    // rather than on the CRC. The CRCs were generated with CRC-16/CCITT-FALSE (Python's binascii.crc_hqx
    // with a 0xFFFF preset), independently of uslp.c

    // SCID 0xABCD, VCID 5, MAP 3, expedited, 1-octet VC count of 0, rule 111, UPID 0, carrying an
    // 8-byte space packet. These are exactly the bytes uslp_mapp_request builds for that packet
    uint8_t basic[] = {0xCA, 0xBC, 0xD0, 0xA6, 0x00, 0x12, 0x81, 0x00, 0xE0, 0x08, 0x01, 0xC0, 0x00, 0x00, 0x01, 0xAA, 0xBB, 0x08, 0x3B};
    uint8_t basic_payload[] = {0x08, 0x01, 0xC0, 0x00, 0x00, 0x01, 0xAA, 0xBB};
    uslp_transfer_frame_view_t view;
    bool err;

    test_log("uslp parse basic frame test:\n");
    err = uslp_transfer_frame_parse(&view, basic, sizeof(basic));
    PVDX_ASSERT_MSG(!err, "basic frame parse\n");
    PVDX_ASSERT_MSG(view.primary_header.version_num == USLP_TFVN, "basic version_num\n");
    PVDX_ASSERT_MSG(view.primary_header.spacecraft_id == 0xABCD, "basic spacecraft_id\n");
    PVDX_ASSERT_MSG(view.primary_header.src_or_dest == USLP_SCID_IS_SOURCE, "basic src_or_dest\n");
    PVDX_ASSERT_MSG(view.primary_header.virtual_channel_id == 5, "basic virtual_channel_id\n");
    PVDX_ASSERT_MSG(view.primary_header.map_id == 3, "basic map_id\n");
    PVDX_ASSERT_MSG(view.primary_header.end_of_frame_primary_header_flag == 0, "basic end_of_frame_primary_header_flag\n");
    PVDX_ASSERT_MSG(view.primary_header.frame_length == sizeof(basic) - 1, "basic frame_length\n");
    PVDX_ASSERT_MSG(view.primary_header.bypass_sequence_control_flag == USLP_QOS_EXPEDITED, "basic bypass_sequence_control_flag\n");
    PVDX_ASSERT_MSG(view.primary_header.protocol_control_command_flag == 0, "basic protocol_control_command_flag\n");
    PVDX_ASSERT_MSG(view.primary_header.ocf_flag == 0, "basic ocf_flag\n");
    PVDX_ASSERT_MSG(view.primary_header.vc_frame_count_length == 1, "basic vc_frame_count_length\n");
    PVDX_ASSERT_MSG(view.primary_header.vc_frame_count == 0, "basic vc_frame_count\n");
    PVDX_ASSERT_MSG(view.data_field_header.tfdz_construction_rules == USLP_TFDZ_NO_SEGMENT, "basic tfdz_construction_rules\n");
    PVDX_ASSERT_MSG(view.data_field_header.protocol_identifier == USLP_UPID_SPACE_PACKETS, "basic protocol_identifier\n");
    PVDX_ASSERT_MSG(view.datafield == &basic[9], "basic datafield points into the input\n");
    PVDX_ASSERT_MSG(view.datafield_len == sizeof(basic_payload), "basic datafield_len\n");
    PVDX_ASSERT_MSG(memcmp(view.datafield, basic_payload, sizeof(basic_payload)) == 0, "basic datafield contents\n");

    // SCID 0xFFFF, destination, VCID 62, MAP 15, sequence controlled, protocol control command,
    // 7-octet VC count of 0x00123456789ABCDE, rule 011, UPID 5, a 3-byte payload, and an OCF of 0xDEADBEEF
    test_log("uslp parse max-field frame test:\n");
    uint8_t max[] = {0xCF, 0xFF, 0xFF, 0xDE, 0x00, 0x17, 0x4F, 0x12, 0x34, 0x56, 0x78, 0x9A,
                     0xBC, 0xDE, 0x65, 0x11, 0x22, 0x33, 0xDE, 0xAD, 0xBE, 0xEF, 0x9D, 0x14};
    uint8_t max_payload[] = {0x11, 0x22, 0x33};
    err = uslp_transfer_frame_parse(&view, max, sizeof(max));
    PVDX_ASSERT_MSG(!err, "max frame parse\n");
    PVDX_ASSERT_MSG(view.primary_header.spacecraft_id == 0xFFFF, "max spacecraft_id\n");
    PVDX_ASSERT_MSG(view.primary_header.src_or_dest == USLP_SCID_IS_DESTINATION, "max src_or_dest\n");
    PVDX_ASSERT_MSG(view.primary_header.virtual_channel_id == 62, "max virtual_channel_id\n");
    PVDX_ASSERT_MSG(view.primary_header.map_id == 15, "max map_id\n");
    PVDX_ASSERT_MSG(view.primary_header.bypass_sequence_control_flag == USLP_QOS_SEQUENCE_CONTROLLED, "max bypass_sequence_control_flag\n");
    PVDX_ASSERT_MSG(view.primary_header.protocol_control_command_flag == 1, "max protocol_control_command_flag\n");
    PVDX_ASSERT_MSG(view.primary_header.ocf_flag == 1, "max ocf_flag\n");
    PVDX_ASSERT_MSG(view.primary_header.vc_frame_count_length == 7, "max vc_frame_count_length\n");
    PVDX_ASSERT_MSG(view.primary_header.vc_frame_count == 0x00123456789ABCDEull, "max vc_frame_count\n");
    PVDX_ASSERT_MSG(view.data_field_header.tfdz_construction_rules == USLP_TFDZ_BYTE_STREAM, "max tfdz_construction_rules\n");
    PVDX_ASSERT_MSG(view.data_field_header.protocol_identifier == 5, "max protocol_identifier\n");
    // The OCF sits between the data zone and the FECF, and must not be counted as data
    PVDX_ASSERT_MSG(view.datafield_len == sizeof(max_payload), "max datafield_len excludes OCF\n");
    PVDX_ASSERT_MSG(memcmp(view.datafield, max_payload, sizeof(max_payload)) == 0, "max datafield contents\n");

    // SCID 0x1234, VCID 1, no VC count at all, and an empty data zone
    test_log("uslp parse empty frame test:\n");
    uint8_t empty[] = {0xC1, 0x23, 0x40, 0x20, 0x00, 0x09, 0x80, 0xE0, 0xD1, 0x4B};
    err = uslp_transfer_frame_parse(&view, empty, sizeof(empty));
    PVDX_ASSERT_MSG(!err, "empty frame parse\n");
    PVDX_ASSERT_MSG(view.primary_header.spacecraft_id == 0x1234, "empty spacecraft_id\n");
    PVDX_ASSERT_MSG(view.primary_header.vc_frame_count_length == 0, "empty vc_frame_count_length\n");
    PVDX_ASSERT_MSG(view.primary_header.vc_frame_count == 0, "empty vc_frame_count\n");
    PVDX_ASSERT_MSG(view.datafield_len == 0, "empty datafield_len\n");

    test_log("uslp parse length tests:\n");
    // Bytes after the end given by the frame length field are ignored
    uint8_t padded[sizeof(basic) + 4] = {0};
    memcpy(padded, basic, sizeof(basic));
    err = uslp_transfer_frame_parse(&view, padded, sizeof(padded));
    PVDX_ASSERT_MSG(!err, "trailing bytes accepted\n");
    PVDX_ASSERT_MSG(view.datafield_len == sizeof(basic_payload), "trailing bytes not counted as data\n");

    PVDX_ASSERT_MSG(uslp_transfer_frame_parse(&view, basic, sizeof(basic) - 1), "input shorter than frame length rejected\n");
    PVDX_ASSERT_MSG(uslp_transfer_frame_parse(&view, basic, USLP_PRIMARY_HEADER_FIXED_SIZE - 1), "input shorter than header rejected\n");
    PVDX_ASSERT_MSG(uslp_transfer_frame_parse(&view, basic, 0), "empty input rejected\n");

    // Header claims a 7-octet VC count and an OCF, but the frame length only covers 10 octets
    uint8_t too_short[] = {0xC0, 0x00, 0x00, 0x00, 0x00, 0x09, 0x0F, 0x00, 0xB4, 0xC0};
    PVDX_ASSERT_MSG(uslp_transfer_frame_parse(&view, too_short, sizeof(too_short)), "frame too short for its headers rejected\n");

    test_log("uslp parse FECF tests:\n");
    uint8_t corrupted[sizeof(basic)];

    memcpy(corrupted, basic, sizeof(basic));
    corrupted[12] ^= 0x01; // one bit of the data zone
    PVDX_ASSERT_MSG(uslp_transfer_frame_parse(&view, corrupted, sizeof(corrupted)), "corrupted data zone rejected\n");

    memcpy(corrupted, basic, sizeof(basic));
    corrupted[1] ^= 0x10; // one bit of the spacecraft ID
    PVDX_ASSERT_MSG(uslp_transfer_frame_parse(&view, corrupted, sizeof(corrupted)), "corrupted header rejected\n");

    memcpy(corrupted, basic, sizeof(basic));
    corrupted[sizeof(corrupted) - 1] ^= 0x80; // one bit of the FECF itself
    PVDX_ASSERT_MSG(uslp_transfer_frame_parse(&view, corrupted, sizeof(corrupted)), "corrupted FECF rejected\n");

    test_log("uslp parse unsupported frame tests:\n");
    memcpy(corrupted, basic, sizeof(basic));
    corrupted[0] &= 0x0F; // TFVN 0000 (a TM frame), which is checked before the FECF
    PVDX_ASSERT_MSG(uslp_transfer_frame_parse(&view, corrupted, sizeof(corrupted)), "wrong TFVN rejected\n");

    // Same as basic, but with the End of Frame Primary Header flag set (a truncated frame)
    uint8_t truncated[] = {0xCA, 0xBC, 0xD0, 0xA7, 0x00, 0x12, 0x81, 0x00, 0xE0, 0x08,
                           0x01, 0xC0, 0x00, 0x00, 0x01, 0xAA, 0xBB, 0x73, 0x5A};
    PVDX_ASSERT_MSG(uslp_transfer_frame_parse(&view, truncated, sizeof(truncated)), "truncated frame rejected\n");

    // Same as basic, but with TFDZ construction rule 000, which needs a First Header Pointer
    uint8_t rule_000[] = {0xCA, 0xBC, 0xD0, 0xA6, 0x00, 0x12, 0x81, 0x00, 0x00, 0x08, 0x01, 0xC0, 0x00, 0x00, 0x01, 0xAA, 0xBB, 0x4F, 0xAC};
    PVDX_ASSERT_MSG(uslp_transfer_frame_parse(&view, rule_000, sizeof(rule_000)), "TFDZ rule 000 rejected\n");

    #ifdef UNITTEST // uslp_test_last_frame only exists in unit test builds
    // VC frame counts are module state that persists between calls, so these tests assume nothing
    // has sent on VCs 5-7 since boot. That holds because tests run before any task is started
    test_log("uslp mapp request rejection tests:\n");
    uint32_t gmap_id = ((uint32_t)USLP_TFVN << 26) | (0xABCDu << 10) | (5u << 4) | 3u;
    uint32_t gmap_id_bad_tfvn = (0xABCDu << 10) | (5u << 4) | 3u;
    uint32_t gmap_id_idle_vc = ((uint32_t)USLP_TFVN << 26) | (0xABCDu << 10) | ((uint32_t)USLP_IDLE_ONLY_FRAME_INDEX << 4);
    uint32_t gmap_id_vc6 = ((uint32_t)USLP_TFVN << 26) | (0xABCDu << 10) | (6u << 4);
    uint32_t gmap_id_vc7 = ((uint32_t)USLP_TFVN << 26) | (0xABCDu << 10) | (7u << 4);
    const uint8_t *last_frame;
    uint32_t last_frame_len;

    err = uslp_mapp_request(basic_payload, sizeof(basic_payload), gmap_id_bad_tfvn, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(err, "mapp request with wrong TFVN rejected\n");

    err = uslp_mapp_request(basic_payload, sizeof(basic_payload), gmap_id_idle_vc, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(err, "mapp request on idle-only VC 63 rejected\n");

    err = uslp_mapp_request(basic_payload, sizeof(basic_payload), gmap_id, 7, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(err, "mapp request with non-SPP PVN rejected\n");

    err = uslp_mapp_request(basic_payload, sizeof(basic_payload), gmap_id, SPP_VERSION_NUMBER, 0, USLP_QOS_SEQUENCE_CONTROLLED);
    PVDX_ASSERT_MSG(err, "mapp request with sequence controlled QoS rejected\n");

    uslp_test_last_frame(&last_frame_len);
    PVDX_ASSERT_MSG(last_frame_len == 0, "rejected mapp requests send nothing\n");

    // The parser needs a writable buffer, as it would get from the radio
    static uint8_t sent[USLP_MAX_FRAME_SIZE];

    test_log("uslp mapp request send tests:\n");
    err = uslp_mapp_request(basic_payload, sizeof(basic_payload), gmap_id, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(!err, "mapp request sent\n");
    last_frame = uslp_test_last_frame(&last_frame_len);
    // The first frame on VC 5 has a count of 0, so it must match the independently built vector exactly
    PVDX_ASSERT_MSG(last_frame_len == sizeof(basic), "first frame length\n");
    PVDX_ASSERT_MSG(memcmp(last_frame, basic, sizeof(basic)) == 0, "first frame matches reference bytes\n");

    err = uslp_mapp_request(basic_payload, sizeof(basic_payload), gmap_id, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(!err, "second mapp request sent\n");
    last_frame = uslp_test_last_frame(&last_frame_len);
    memcpy(sent, last_frame, last_frame_len);
    err = uslp_transfer_frame_parse(&view, sent, last_frame_len);
    PVDX_ASSERT_MSG(!err, "second frame parses\n");
    PVDX_ASSERT_MSG(view.primary_header.vc_frame_count == 1, "second frame count\n");
    PVDX_ASSERT_MSG(view.datafield_len == sizeof(basic_payload), "second frame datafield_len\n");
    PVDX_ASSERT_MSG(memcmp(view.datafield, basic_payload, sizeof(basic_payload)) == 0, "second frame datafield contents\n");

    test_log("uslp mapp request frame size tests:\n");
    // Largest packet that still fits in one frame with a 1-octet VC count
    static uint8_t big_payload[USLP_MAX_FRAME_SIZE];
    uint16_t max_payload_len = USLP_MAX_FRAME_SIZE - USLP_PRIMARY_HEADER_FIXED_SIZE - 1 - USLP_DATA_FIELD_HEADER_SIZE - USLP_FECF_SIZE;
    for (uint16_t i = 0; i < sizeof(big_payload); i++) {
        big_payload[i] = (uint8_t)(i * 7);
    }

    err = uslp_mapp_request(big_payload, max_payload_len + 1, gmap_id, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(err, "oversized mapp request rejected\n");
    uslp_test_last_frame(&last_frame_len);
    PVDX_ASSERT_MSG(last_frame_len == sizeof(basic), "oversized mapp request sends nothing\n");

    // A rejected frame must not use up a count, so the next frame on VC 5 is 2, not 3
    err = uslp_mapp_request(basic_payload, sizeof(basic_payload), gmap_id, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(!err, "mapp request after oversized request sent\n");
    last_frame = uslp_test_last_frame(&last_frame_len);
    memcpy(sent, last_frame, last_frame_len);
    err = uslp_transfer_frame_parse(&view, sent, last_frame_len);
    PVDX_ASSERT_MSG(!err, "frame after oversized request parses\n");
    PVDX_ASSERT_MSG(view.primary_header.vc_frame_count == 2, "no count gap after oversized request\n");

    err = uslp_mapp_request(big_payload, max_payload_len, gmap_id_vc7, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(!err, "max size mapp request sent\n");
    last_frame = uslp_test_last_frame(&last_frame_len);
    PVDX_ASSERT_MSG(last_frame_len == USLP_MAX_FRAME_SIZE, "max size frame fills USLP_MAX_FRAME_SIZE\n");
    memcpy(sent, last_frame, last_frame_len);
    err = uslp_transfer_frame_parse(&view, sent, last_frame_len);
    PVDX_ASSERT_MSG(!err, "max size frame parses\n");
    PVDX_ASSERT_MSG(view.primary_header.virtual_channel_id == 7, "max size frame virtual_channel_id\n");
    PVDX_ASSERT_MSG(view.datafield_len == max_payload_len, "max size frame datafield_len\n");
    PVDX_ASSERT_MSG(memcmp(view.datafield, big_payload, max_payload_len) == 0, "max size frame datafield contents\n");

    test_log("uslp mapp request per-VC count tests:\n");
    // Each VC keeps its own count, so VC 6 starts at 0 even though VC 5 has sent 3 frames
    err = uslp_mapp_request(basic_payload, sizeof(basic_payload), gmap_id_vc6, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(!err, "mapp request on VC 6 sent\n");
    last_frame = uslp_test_last_frame(&last_frame_len);
    memcpy(sent, last_frame, last_frame_len);
    err = uslp_transfer_frame_parse(&view, sent, last_frame_len);
    PVDX_ASSERT_MSG(!err, "VC 6 frame parses\n");
    PVDX_ASSERT_MSG(view.primary_header.virtual_channel_id == 6, "VC 6 frame virtual_channel_id\n");
    PVDX_ASSERT_MSG(view.primary_header.vc_frame_count == 0, "VC 6 has its own count\n");
    #endif
}
#endif // TEST_USLP
// #endif

// Tests that the hard float ABI works: FPU arithmetic, float args/returns passed in s registers, and libm calls.
// Operands are volatile and the helpers are noinline so the compiler can't constant fold the work away.
#define FLOAT_EPS 1e-5f
static bool float_close(float a, float b) {
    return fabsf(a - b) < FLOAT_EPS;
}

__attribute__((noinline)) static float float_mul_add(float a, float b, float c) {
    return a * b + c;
}

// Mixes int and float args so the ints go in r registers and the floats in s registers
__attribute__((noinline)) static float float_scale_sum(int n, float x, int m, float y) {
    return (float)n * x + (float)m * y;
}

// More float args than the 16 s registers, so the last ones are passed on the stack
__attribute__((noinline)) static float float_sum18(float a0, float a1, float a2, float a3, float a4, float a5, float a6, float a7, float a8,
                                                   float a9, float a10, float a11, float a12, float a13, float a14, float a15, float a16,
                                                   float a17) {
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12 + a13 + a14 + a15 + a16 + a17;
}

__attribute__((noinline)) static void float_vec_scale(float *v, int len, float k) {
    for (int i = 0; i < len; i++) {
        v[i] *= k;
    }
}

void test_float(void) {
    test_log("----- testing float -----\n");
    volatile float a = 1.5f;
    volatile float b = 2.25f;
    volatile float c = -4.0f;

    test_log("float arithmetic tests:\n");
    PVDX_ASSERT_MSG(float_close(a + b, 3.75f), "float add\n");
    PVDX_ASSERT_MSG(float_close(a - b, -0.75f), "float sub\n");
    PVDX_ASSERT_MSG(float_close(a * c, -6.0f), "float mul\n");
    PVDX_ASSERT_MSG(float_close(b / a, 1.5f), "float div\n");
    PVDX_ASSERT_MSG(float_close(1.0f / 3.0f * a, 0.5f), "float div then mul\n");
    PVDX_ASSERT_MSG(a < b && c < a, "float compare\n");
    PVDX_ASSERT_MSG((int)(b * c) == -9, "float to int conversion\n");

    test_log("float function call tests:\n");
    PVDX_ASSERT_MSG(float_close(float_mul_add(a, b, c), -0.625f), "float args and return\n");
    PVDX_ASSERT_MSG(float_close(float_scale_sum(3, a, -2, b), 0.0f), "mixed int and float args\n");
    PVDX_ASSERT_MSG(float_close(float_sum18(a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, b, c), 22.25f), "float args on stack\n");

    float v[3] = {a, b, c};
    float_vec_scale(v, 3, 2.0f);
    PVDX_ASSERT_MSG(float_close(v[0], 3.0f) && float_close(v[1], 4.5f) && float_close(v[2], -8.0f), "float pointer arg\n");

    test_log("libm float tests:\n");
    PVDX_ASSERT_MSG(float_close(sqrtf(b), a), "sqrtf\n");
    PVDX_ASSERT_MSG(float_close(fabsf(c), 4.0f), "fabsf\n");
    PVDX_ASSERT_MSG(float_close(sinf(0.0f) + cosf(0.0f), 1.0f), "sinf + cosf\n");
    PVDX_ASSERT_MSG(float_close(atan2f(a, a), (float)M_PI / 4.0f), "atan2f\n");
}
