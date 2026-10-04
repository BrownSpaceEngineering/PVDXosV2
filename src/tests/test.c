
#include "tests/test.h"

#include "cfdp/cfdp_pdu.h"
#include "cfdp/cfdp_task.h"
#include "cfdp/cfdp_utils.h"
#include "linalg/LinearAlgebra/declareFunctions.h"
#include "logging.h"
#include "radio/spp.h"

#if defined(UNITTEST)
uint8_t test_mem[10000];
#endif

void test_spp(void);
void test_matrix_product(void);
void test_cfdp(void);

void tests_run(void) {
    test_spp();
    test_matrix_product();
    test_cfdp();
}

// #ifdef UNITTEST
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

void test_matrix_product(void) {
    test_log("----- testing matrix product -----\n");

    // test case for 2*2 matrix product
    float A[4] = {1., 2., 3., 4.};
    float B[4] = {5., 6., 7., 8.};
    float C[4] = {0.};
    float C_expected[4] = {19., 22., 43., 50.};

    mul(A, B, false, C, 2, 2, 2);
    // log_matrix(C, 2, 2);

    if (dbl_eps_close_matrix(C, C_expected, 2, 2, DBL_EPSILON)) {
        test_log("1 * 2 matrix product test passed!\n");
    } else {
        test_log("2 * 2 matrix product test failed!\n");
    }

    // 4*4 identity matrix test
    float identity[4 * 4] = {0.};
    float large_result[4 * 4] = {0.};

    eye(identity, 4, 4);

    mul(identity, identity, false, large_result, 4, 4, 4);

    if (dbl_eps_close_matrix(identity, large_result, 4, 4, DBL_EPSILON)) {
        test_log("Identity matrix squared test passed!\n");
    } else {
        test_log("Identity matrix squared test failed!\n");
    }

    float A_large[4 * 4] = {1., 2., 3., 4., 5., 6., 7., 8., 9., 10., 11., 12., 13., 14., 15., 16.};

    mul(A_large, identity, false, large_result, 4, 4, 4);

    if (dbl_eps_close_matrix(A_large, large_result, 4, 4, DBL_EPSILON)) {
        test_log("Identity post-multiplication test passed!\n");
    } else {
        test_log("Identity post-multiplication test failed!\n");
    }

    mul(identity, A_large, false, large_result, 4, 4, 4);

    if (dbl_eps_close_matrix(A_large, large_result, 4, 4, DBL_EPSILON)) {
        test_log("Identity pre-multiplication test passed!\n");
    } else {
        test_log("Identity pre-multiplication test failed!\n");
    }

    float B_large[4 * 4] = {5.24829, 6.21496, 3.27374, 3.49223, 1.52040, 3.70849, 7.21884, 0.41667,
                            7.77438, 8.24807, 8.63347, 2.01096, 8.29170, 1.46735, 8.53606, 5.14221};

    float C_large[4 * 4] = {1.92304, 0.14043, 1.64762, 4.97396, 0.68077, 4.99275, 7.04041, 2.44857,
                            8.22049, 1.66745, 7.94150, 0.56302, 2.68638, 7.59450, 1.43236, 3.59834};

    float large_multiplication_expected[4 * 4] = {50.6168, 63.7473, 83.4036, 55.732,  65.9102, 33.9305, 86.5396, 22.2066,
                                                  96.939,  71.9404, 142.322, 70.9624, 100.929, 61.7765, 99.1469, 68.1449};

    mul(B_large, C_large, false, large_result, 4, 4, 4);

    if (dbl_eps_close_matrix(large_result, large_multiplication_expected, 4, 4, DBL_EPSILON)) {
        test_log("Large matrix product test passed!\n");
    } else {
        test_log("Large matrix product test failed!\n");
    }
}

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
// #endif
