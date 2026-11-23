
#include "tests/test.h"

#include <math.h>
#include <string.h>

#include "ccsds/spp.h"
#include "ccsds/uslp.h"
#include "cfdp/cfdp_pdu.h"
#include "cfdp/cfdp_task.h"
#include "cfdp/cfdp_utils.h"
#include "cfdp/uplink_commands.h"
#include "cfdp/uplink_parse.h"
#include "checks/device_checks.h"
#include "drivers/pmb/adg734.h"
#include "drivers/pmb/ina226.h"
#include "drivers/pmb/mcp23017.h"
#include "drivers/pmb/pmb_i2c.h"
#include "drivers/pmb/pvd_dac.h"
#include "drivers/watchdog/watchdog_driver.h"
#include "logging.h"
#include "os_delay.h"
#include "telemetry/telemetry.h"
#include "tests/test_pusp.h"
#include "utc.h"

// test.c is compiled in every build, but the tests use hooks (test_mem, uslp_test_last_frame) that only unit test builds
// have. Which tests run is chosen in tests_run(); the linker drops the rest.
#ifdef UNITTEST

int tests_passed = 0;
int tests_total = 0;

uint8_t test_mem[512];

void test_spp(void);
void test_matrix_product(void);
void test_cfdp(void);
void test_uslp(void);
void test_telemetry_downlink(void);
void test_telemetry_setters(void);
void test_uplink_parse(void);
void test_float(void);
void test_pmb(void);

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
#ifdef TEST_PMB
    test_pmb();
#endif
#ifdef TEST_FLOAT
    test_float();
#endif
#ifdef TEST_TELEMETRY
    test_telemetry_downlink();
    test_telemetry_setters();
#endif
#ifdef TEST_UPLINK
    test_uplink_parse();
#endif
#ifdef TEST_PUSP
    test_pusp();
#endif
    test_log("test results: %d/%d passed", tests_passed, tests_total);
}

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

    // Packets below were generated with Python's struct module, independently of spp.c.
    // Each assert costs RAM in the test image, so data contents are covered by checking that view.data
    // points to the right place in the (unchanged) input, rather than by comparing bytes

    // Telemetry, APID 0x123, unsegmented, count 0x2A, with a secondary header (timestamp 0x01020304,
    // boot_timestamp 0x0A0B0C0D, callsign "BSEBSE") and 3 bytes of user data. Every integer byte is distinct,
    // so a swapped or shifted field shows up clearly
    uint8_t with_sec[] = {0x09, 0x23, 0xC0, 0x2A, 0x00, 0x10, 0x01, 0x02, 0x03, 0x04, 0x0A, 0x0B,
                          0x0C, 0x0D, 0x42, 0x53, 0x45, 0x42, 0x53, 0x45, 0xAA, 0xBB, 0xCC};
    spp_packet_view_t view;
    bool err;

    test_log("spp parse tests:\n");
    err = spp_packet_parse(&view, with_sec, sizeof(with_sec));
    PVDX_ASSERT_MSG(!err, "with_sec parse\n");
    PVDX_ASSERT_MSG(view.header.version_number == SPP_VERSION_NUMBER, "with_sec version\n");
    PVDX_ASSERT_MSG(view.header.packet_type == SPP_PACKET_TYPE_REPORTING, "with_sec type\n");
    PVDX_ASSERT_MSG(view.header.secondary_header_flag == SPP_SECONDARY_HEADER_PRESENT, "with_sec sec flag\n");
    PVDX_ASSERT_MSG(view.header.application_process_id == 0x123, "with_sec apid\n");
    PVDX_ASSERT_MSG(view.header.sequence_flags == SPP_SEQ_FLAG_UNSEGMENTED_DATA, "with_sec seq flags\n");
    PVDX_ASSERT_MSG(view.header.sequence_count == 0x2A, "with_sec seq count\n");
    PVDX_ASSERT_MSG(view.header.data_length == sizeof(with_sec) - SPP_PRIMARY_HEADER_SIZE - 1, "with_sec data_length\n");
    PVDX_ASSERT_MSG(view.secondary_header.timestamp == 0x01020304, "with_sec timestamp\n");
    PVDX_ASSERT_MSG(view.secondary_header.boot_timestamp == 0x0A0B0C0D, "with_sec boot_timestamp\n");
    PVDX_ASSERT_MSG(memcmp(view.secondary_header.callsign, "BSEBSE", SPP_SECONDARY_HEADER_CALLSIGN_SIZE) == 0, "with_sec callsign\n");
    PVDX_ASSERT_MSG(view.data == &with_sec[SPP_PRIMARY_HEADER_SIZE + SPP_SECONDARY_HEADER_SIZE], "with_sec data\n");

    // Telecommand, APID 0x7FE, first segment, max count 0x3FFF, no secondary header, 2 bytes of user data
    uint8_t no_sec[] = {0x17, 0xFE, 0x7F, 0xFF, 0x00, 0x01, 0x11, 0x22};
    view.secondary_header.timestamp = 0xFFFFFFFF; // left over from a previous parse, must be cleared
    err = spp_packet_parse(&view, no_sec, sizeof(no_sec));
    PVDX_ASSERT_MSG(!err, "no_sec parse\n");
    PVDX_ASSERT_MSG(view.header.packet_type == SPP_PACKET_TYPE_REQUESTING, "no_sec type\n");
    PVDX_ASSERT_MSG(view.header.secondary_header_flag == SPP_SECONDARY_HEADER_NOT_PRESENT, "no_sec sec flag\n");
    PVDX_ASSERT_MSG(view.header.application_process_id == 0x7FE, "no_sec apid\n");
    PVDX_ASSERT_MSG(view.header.sequence_flags == SPP_SEQ_FLAG_FIRST_SEGMENT_OF_DATA, "no_sec seq flags\n");
    PVDX_ASSERT_MSG(view.header.sequence_count == 0x3FFF, "no_sec seq count\n");
    PVDX_ASSERT_MSG(view.secondary_header.timestamp == 0, "no_sec secondary header zeroed\n");
    PVDX_ASSERT_MSG(view.data == &no_sec[SPP_PRIMARY_HEADER_SIZE], "no_sec data\n");

    // APID 1, a secondary header with high bits set (timestamp 0xFFFFFFFF, boot_timestamp 0x80000001,
    // callsign "ABCDEF") and no user data
    uint8_t sec_only[] = {0x08, 0x01, 0xC0, 0x00, 0x00, 0x0D, 0xFF, 0xFF, 0xFF, 0xFF,
                          0x80, 0x00, 0x00, 0x01, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46};
    err = spp_packet_parse(&view, sec_only, sizeof(sec_only));
    PVDX_ASSERT_MSG(!err && view.secondary_header.timestamp == 0xFFFFFFFF && view.secondary_header.boot_timestamp == 0x80000001 &&
                        memcmp(view.secondary_header.callsign, "ABCDEF", SPP_SECONDARY_HEADER_CALLSIGN_SIZE) == 0,
                    "sec_only secondary header\n");
    PVDX_ASSERT_MSG(view.data == &sec_only[sizeof(sec_only)], "sec_only data\n");

    // Bytes after the end given by the packet data length field are ignored
    uint8_t padded[sizeof(with_sec) + 4] = {0};
    memcpy(padded, with_sec, sizeof(with_sec));
    PVDX_ASSERT_MSG(!spp_packet_parse(&view, padded, sizeof(padded)), "trailing bytes accepted\n");

    test_log("spp parse rejection tests:\n");
    PVDX_ASSERT_MSG(spp_packet_parse(&view, with_sec, sizeof(with_sec) - 1), "shorter than data length\n");
    PVDX_ASSERT_MSG(spp_packet_parse(&view, with_sec, SPP_PRIMARY_HEADER_SIZE - 1), "shorter than primary header\n");
    PVDX_ASSERT_MSG(spp_packet_parse(NULL, with_sec, sizeof(with_sec)) && spp_packet_parse(&view, NULL, sizeof(with_sec)), "NULL args\n");

    // Secondary header flag is set, but the packet data field is one octet short of a full secondary header
    uint8_t short_sec[] = {0x08, 0x01, 0xC0, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x01,
                           0x00, 0x00, 0x00, 0x02, 0x41, 0x42, 0x43, 0x44, 0x45};
    PVDX_ASSERT_MSG(spp_packet_parse(&view, short_sec, sizeof(short_sec)), "too short for secondary header\n");

    with_sec[0] |= 0x20; // version number 001
    PVDX_ASSERT_MSG(spp_packet_parse(&view, with_sec, sizeof(with_sec)), "wrong version number\n");
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
                              .stream = RADIO_STREAM_IMAGE,
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

    // VC frame counts are module state that persists between calls, so these tests assume nothing
    // has sent on VCs 5-7 since boot. That holds because tests run before any task is started
    test_log("uslp mapp request rejection tests:\n");
    uint32_t gmap_id = ((uint32_t)USLP_TFVN << 26) | (0xABCDu << 10) | (5u << 4) | 3u;
    uint32_t gmap_id_bad_tfvn = (0xABCDu << 10) | (5u << 4) | 3u;
    uint32_t gmap_id_idle_vc = ((uint32_t)USLP_TFVN << 26) | (0xABCDu << 10) | ((uint32_t)USLP_IDLE_ONLY_FRAME_INDEX << 4);
    uint32_t gmap_id_vc6 = ((uint32_t)USLP_TFVN << 26) | (0xABCDu << 10) | (6u << 4);
    uint32_t gmap_id_vc7 = ((uint32_t)USLP_TFVN << 26) | (0xABCDu << 10) | (7u << 4);
    // Static so a full-size frame doesn't use up the test's stack
    static uint8_t sent[USLP_MAX_FRAME_SIZE];
    size_t sent_len;

    sent_len = sizeof(sent);
    err = uslp_mapp_request(sent, &sent_len, basic_payload, sizeof(basic_payload), gmap_id_bad_tfvn, SPP_VERSION_NUMBER, 0,
                            USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(err, "mapp request with wrong TFVN rejected\n");
    PVDX_ASSERT_MSG(sent_len == 0, "mapp request with wrong TFVN writes nothing\n");

    sent_len = sizeof(sent);
    err = uslp_mapp_request(sent, &sent_len, basic_payload, sizeof(basic_payload), gmap_id_idle_vc, SPP_VERSION_NUMBER, 0,
                            USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(err, "mapp request on idle-only VC 63 rejected\n");
    PVDX_ASSERT_MSG(sent_len == 0, "mapp request on idle-only VC 63 writes nothing\n");

    sent_len = sizeof(sent);
    err = uslp_mapp_request(sent, &sent_len, basic_payload, sizeof(basic_payload), gmap_id, 7, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(err, "mapp request with non-SPP PVN rejected\n");
    PVDX_ASSERT_MSG(sent_len == 0, "mapp request with non-SPP PVN writes nothing\n");

    sent_len = sizeof(sent);
    err = uslp_mapp_request(sent, &sent_len, basic_payload, sizeof(basic_payload), gmap_id, SPP_VERSION_NUMBER, 0,
                            USLP_QOS_SEQUENCE_CONTROLLED);
    PVDX_ASSERT_MSG(err, "mapp request with sequence controlled QoS rejected\n");
    PVDX_ASSERT_MSG(sent_len == 0, "mapp request with sequence controlled QoS writes nothing\n");

    // One byte short of what the basic frame needs
    sent_len = sizeof(basic) - 1;
    err = uslp_mapp_request(sent, &sent_len, basic_payload, sizeof(basic_payload), gmap_id, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(err, "mapp request into too small a buffer rejected\n");
    PVDX_ASSERT_MSG(sent_len == 0, "mapp request into too small a buffer writes nothing\n");

    PVDX_ASSERT_MSG(
        uslp_mapp_request(NULL, &sent_len, basic_payload, sizeof(basic_payload), gmap_id, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED),
        "mapp request with NULL buffer rejected\n");

    test_log("uslp mapp request send tests:\n");
    // A buffer exactly the size of the frame is enough
    sent_len = sizeof(basic);
    err = uslp_mapp_request(sent, &sent_len, basic_payload, sizeof(basic_payload), gmap_id, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(!err, "mapp request sent\n");
    // The first frame on VC 5 has a count of 0 (rejected requests above must not have used one up),
    // so it must match the independently built vector exactly
    PVDX_ASSERT_MSG(sent_len == sizeof(basic), "first frame length\n");
    PVDX_ASSERT_MSG(memcmp(sent, basic, sizeof(basic)) == 0, "first frame matches reference bytes\n");

    sent_len = sizeof(sent);
    err = uslp_mapp_request(sent, &sent_len, basic_payload, sizeof(basic_payload), gmap_id, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(!err, "second mapp request sent\n");
    PVDX_ASSERT_MSG(sent_len == sizeof(basic), "second frame length\n");
    err = uslp_transfer_frame_parse(&view, sent, sent_len);
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

    // Too big for USLP_MAX_FRAME_SIZE, even though the buffer itself is bigger than that
    static uint8_t big_buf[USLP_MAX_FRAME_SIZE + 16];
    sent_len = sizeof(big_buf);
    err = uslp_mapp_request(big_buf, &sent_len, big_payload, max_payload_len + 1, gmap_id, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(err, "oversized mapp request rejected\n");
    PVDX_ASSERT_MSG(sent_len == 0, "oversized mapp request writes nothing\n");

    // A rejected frame must not use up a count, so the next frame on VC 5 is 2, not 3
    sent_len = sizeof(sent);
    err = uslp_mapp_request(sent, &sent_len, basic_payload, sizeof(basic_payload), gmap_id, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(!err, "mapp request after oversized request sent\n");
    err = uslp_transfer_frame_parse(&view, sent, sent_len);
    PVDX_ASSERT_MSG(!err, "frame after oversized request parses\n");
    PVDX_ASSERT_MSG(view.primary_header.vc_frame_count == 2, "no count gap after oversized request\n");

    sent_len = sizeof(sent);
    err = uslp_mapp_request(sent, &sent_len, big_payload, max_payload_len, gmap_id_vc7, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(!err, "max size mapp request sent\n");
    PVDX_ASSERT_MSG(sent_len == USLP_MAX_FRAME_SIZE, "max size frame fills USLP_MAX_FRAME_SIZE\n");
    err = uslp_transfer_frame_parse(&view, sent, sent_len);
    PVDX_ASSERT_MSG(!err, "max size frame parses\n");
    PVDX_ASSERT_MSG(view.primary_header.virtual_channel_id == 7, "max size frame virtual_channel_id\n");
    PVDX_ASSERT_MSG(view.datafield_len == max_payload_len, "max size frame datafield_len\n");
    PVDX_ASSERT_MSG(memcmp(view.datafield, big_payload, max_payload_len) == 0, "max size frame datafield contents\n");

    test_log("uslp mapp request per-VC count tests:\n");
    // Each VC keeps its own count, so VC 6 starts at 0 even though VC 5 has sent 3 frames
    sent_len = sizeof(sent);
    err = uslp_mapp_request(sent, &sent_len, basic_payload, sizeof(basic_payload), gmap_id_vc6, SPP_VERSION_NUMBER, 0, USLP_QOS_EXPEDITED);
    PVDX_ASSERT_MSG(!err, "mapp request on VC 6 sent\n");
    err = uslp_transfer_frame_parse(&view, sent, sent_len);
    PVDX_ASSERT_MSG(!err, "VC 6 frame parses\n");
    PVDX_ASSERT_MSG(view.primary_header.virtual_channel_id == 6, "VC 6 frame virtual_channel_id\n");
    PVDX_ASSERT_MSG(view.primary_header.vc_frame_count == 0, "VC 6 has its own count\n");
}

// Floats are compared bit for bit, since parsing must give back exactly the value that was sent
static bool same_float(float a, float b) {
    return memcmp(&a, &b, sizeof(a)) == 0;
}

void test_telemetry_downlink(void) {
    test_log("----- testing telemetry downlink -----\n");

    // Static so these don't use up the test's stack
    static tel_downlink_t downlink;
    // Larger than a downlink packet, so we can check nothing is written past one
    static uint8_t buf[TELEMETRY_DOWNLINK_SIZE + 8];
    bool err;

    memset(&downlink, 0, sizeof(downlink));
    downlink.timestamp = 0x01020304;
    downlink.boot_timestamp = 0x0A0B0C0D;
    downlink.last_bootloader = 2;
    downlink.magnetometer.revid_register = 0x5A;
    downlink.magnetometer.bist_register = 0xA5;
    downlink.magnetometer.raw_readings[0] = 1;
    downlink.magnetometer.raw_readings[1] = -2;
    downlink.magnetometer.raw_readings[2] = 0x12345678;
    downlink.magnetometer.gain_adjusted_readings[0] = 1.0f;
    downlink.magnetometer.gain_adjusted_readings[1] = -2.5f;
    downlink.magnetometer.gain_adjusted_readings[2] = 0.15625f;
    downlink.sband.health = TEL_DEVICE_STATUS_BROKEN;
    downlink.sband.last_transmission_timestamp = 0xDEADBEEF;
    downlink.photodiode.raw_readings[17] = 0xABCD;
    downlink.state_machine.transition_times[7] = 0x55667788;
    downlink.adcs.opmode = TEL_ADCS_OPMODE_DETUMBLE_ONLY;
    downlink.adcs.flags = TEL_ADCS_FLAG_POINTING | TEL_ADCS_FLAG_IN_SUNLIGHT;
    downlink.adcs.filter_resets = 0x0102;
    downlink.adcs.tle_epoch_unix_time = 0xCAFEF00D;
    downlink.command_timestamps[15] = 0xCAFEBABE;
    downlink.mram.status = 0x39;
    downlink.os_errors.error_timestamps[7] = 0x11223344;

    // The expected bytes below were generated with Python's struct module, independently of telemetry.c.
    // Each assert costs RAM in the test image, so rather than comparing all 512 bytes, these check one section
    // near the start, one in the middle, and the last field, which together pin down every field's size and order
    // Magnetometer, right after the 8 bytes of timestamps and the 1-byte last bootloader
    uint8_t expected_mag[] = {0x5A, 0xA5,                                                              // revid, bist
                              0x00, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFE, 0x12, 0x34, 0x56, 0x78,  // raw
                              0x3F, 0x80, 0x00, 0x00, 0xC0, 0x20, 0x00, 0x00, 0x3E, 0x20, 0x00, 0x00}; // gain adjusted
    // S-band health and last transmission timestamp
    uint8_t expected_sband[] = {TEL_DEVICE_STATUS_BROKEN, 0xDE, 0xAD, 0xBE, 0xEF};
    // End of the ADCS section: opmode, flags, filter resets and TLE epoch, after its 15 floats
    uint8_t expected_adcs_tail[] = {0x01, 0x09, 0x01, 0x02, 0xCA, 0xFE, 0xF0, 0x0D};

    test_log("telemetry downlink serialize test:\n");
    memset(buf, 0xEE, sizeof(buf)); // nonzero, so we can tell the spare bytes were zeroed
    err = telemetry_downlink_serialize(&downlink, buf, sizeof(buf));
    PVDX_ASSERT_MSG(!err, "downlink serialize\n");
    uint8_t expected_timestamps[] = {0x01, 0x02, 0x03, 0x04, 0x0A, 0x0B, 0x0C, 0x0D}; // timestamp, boot timestamp
    PVDX_ASSERT_MSG(memcmp(&buf[0], expected_timestamps, sizeof(expected_timestamps)) == 0, "downlink timestamps (no spp headers)\n");
    PVDX_ASSERT_MSG(buf[8] == 2, "downlink last bootloader\n");
    PVDX_ASSERT_MSG(memcmp(&buf[9], expected_mag, sizeof(expected_mag)) == 0, "downlink magnetometer bytes\n");
    PVDX_ASSERT_MSG(buf[81] == 0xAB && buf[82] == 0xCD, "downlink last photodiode (uint16)\n");
    PVDX_ASSERT_MSG(memcmp(&buf[113], expected_sband, sizeof(expected_sband)) == 0, "downlink sband bytes\n");
    PVDX_ASSERT_MSG(buf[218] == 0x55 && buf[219] == 0x66 && buf[220] == 0x77 && buf[221] == 0x88,
                    "downlink last transition time (uint32)\n");
    PVDX_ASSERT_MSG(memcmp(&buf[285], expected_adcs_tail, sizeof(expected_adcs_tail)) == 0, "downlink adcs tail bytes\n");
    PVDX_ASSERT_MSG(buf[353] == 0xCA && buf[354] == 0xFE && buf[355] == 0xBA && buf[356] == 0xBE,
                    "downlink last command timestamp (uint32)\n");
    PVDX_ASSERT_MSG(buf[357] == 0x39, "downlink mram status (1 byte)\n");
    PVDX_ASSERT_MSG(buf[430] == 0x11 && buf[431] == 0x22 && buf[432] == 0x33 && buf[433] == 0x44, "downlink last os error timestamp\n");

    bool spare_zeroed = true;
    for (size_t i = 434; i < TELEMETRY_DOWNLINK_SIZE; i++) {
        spare_zeroed = spare_zeroed && buf[i] == 0;
    }
    PVDX_ASSERT_MSG(spare_zeroed, "downlink spare bytes zeroed\n");
    PVDX_ASSERT_MSG(buf[TELEMETRY_DOWNLINK_SIZE] == 0xEE && buf[sizeof(buf) - 1] == 0xEE, "nothing written past the downlink\n");

    test_log("telemetry downlink rejection tests:\n");
    PVDX_ASSERT_MSG(telemetry_downlink_serialize(&downlink, buf, TELEMETRY_DOWNLINK_SIZE - 1), "too-small buffer rejected\n");
    PVDX_ASSERT_MSG(telemetry_downlink_serialize(NULL, buf, sizeof(buf)) && telemetry_downlink_serialize(&downlink, NULL, sizeof(buf)),
                    "NULL args\n");
}

    // Offsets of fields in a serialized downlink, from the order in telemetry_downlink_serialize()
    #define TEL_OFF_TIMESTAMP 0
    #define TEL_OFF_BOOT_TIMESTAMP 4
    #define TEL_OFF_LAST_BOOTLOADER 8
    #define TEL_OFF_MAG_RAW 11  // after revid and bist
    #define TEL_OFF_MAG_GAIN 23 // gain-adjusted readings
    #define TEL_OFF_MAGNETORQUER 35
    #define TEL_OFF_PHOTODIODE 47
    #define TEL_OFF_GYRO_RAW 83
    #define TEL_OFF_GYRO_RATE 95
    #define TEL_OFF_GYRO_HEALTH 107
    #define TEL_OFF_CAMERA 108 // health, last capture succeeded, resolution
    #define TEL_OFF_DISPLAY_HEALTH 111
    #define TEL_OFF_UHF_HEALTH 112
    #define TEL_OFF_SBAND 113 // health, last transmission timestamp
    #define TEL_OFF_EPS 118   // charge, voltage, current, temperature; 4 floats each
    #define TEL_OFF_STATE_HISTORY 182
    #define TEL_OFF_TRANSITION_TIMES 190
    #define TEL_OFF_STATE_INPUTS 222
    #define TEL_OFF_ADCS 225 // position, attitude, gyro bias, covariance diagonal, then opmode, flags, filter resets, TLE epoch
    #define TEL_OFF_COMMANDS 293
    #define TEL_OFF_MRAM 357 // status, reflash count, reflash timestamps

// The 4 bytes at p, big-endian
static uint32_t tel_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Whether the 4 bytes at p are value, big-endian
static bool tel_be32_is(const uint8_t *p, uint32_t value) {
    return p[0] == (uint8_t)(value >> 24) && p[1] == (uint8_t)(value >> 16) && p[2] == (uint8_t)(value >> 8) && p[3] == (uint8_t)value;
}

// Whether the 4 bytes at p are value's IEEE 754 bits, big-endian. Floats are compared bit for bit, never with ==
static bool tel_be_float_is(const uint8_t *p, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return tel_be32_is(p, bits);
}

static void tel_collect_and_serialize(tel_downlink_t *downlink, uint8_t *buf) {
    telemetry_collect(downlink);
    PVDX_ASSERT_MSG(!telemetry_downlink_serialize(downlink, buf, TELEMETRY_DOWNLINK_SIZE), "setters: serialize\n");
}

// Every setter the firmware calls, through telemetry_collect() and into the serialized downlink, plus the call sites
// that can run before the scheduler. Runs on the live telemetry cache, so it leaves test values in it
void test_telemetry_setters(void) {
    test_log("----- testing telemetry setters -----\n");

    static tel_downlink_t downlink;
    static uint8_t buf[TELEMETRY_DOWNLINK_SIZE];
    bool ok;

    test_log("telemetry setter tests:\n");
    telemetry_update_last_bootloader(1);

    // A good gyro read, then a failed one, which must mark it broken but keep the last good rates
    SCH1_result_t gyro = {.Rate1 = {1.0f, -2.5f, 0.15625f}};
    telemetry_update_gyro_data(&gyro);
    telemetry_collect(&downlink);
    PVDX_ASSERT_MSG(downlink.gyro.health == TEL_DEVICE_STATUS_OK, "gyro OK after a good read\n");
    telemetry_update_gyro_data(NULL);

    telemetry_update_display_data(TEL_DEVICE_STATUS_BROKEN);
    telemetry_update_uhf_data(TEL_DEVICE_STATUS_OK);

    // Each pack separately; pack 4 doesn't exist, so it must be dropped rather than written past the arrays
    for (uint8_t pack = 0; pack < 4; pack++) {
        telemetry_update_eps_data(pack, 25.0f * (pack + 1), 3.5f + pack, -0.25f * (pack + 1), 20.0f + pack);
    }
    telemetry_update_eps_data(4, 99.0f, 99.0f, 99.0f, 99.0f);

    // 9 transitions into an 8-entry history: the first falls out, and the newest is first
    for (uint8_t i = 0; i < 9; i++) {
        telemetry_update_state(i, 1000u + i);
    }

    // 17 uplinks into a 16-entry history
    for (uint32_t i = 0; i < 17; i++) {
        telemetry_update_command(0xC0000000u + i);
    }

    tel_mram_data_t mram = {.status = 0x39, .reflash_count = 3};
    mram.reflash_timestamps[7] = 0x01020304;
    telemetry_update_mram_data(&mram);

    telemetry_update_magnetometer_readings(&(mag_raw_reading_t){.x = 1, .y = -2, .z = 0x12345678},
                                           &(mag_data_t){.x = 1.0f, .y = -2.5f, .z = 0.15625f});
    telemetry_update_magnetorquer_data(&(mag_torque_input_t){.x = 0.5f, .y = -1.0f, .z = 0.25f});

    uint16_t photodiodes[PHD_COUNT];
    for (size_t i = 0; i < PHD_COUNT; i++) {
        photodiodes[i] = (uint16_t)(0x0101u * i + 1);
    }
    telemetry_update_photodiode_data(photodiodes);

    telemetry_update_sband_data(TEL_DEVICE_STATUS_BROKEN, 0xDEADBEEF);

    tel_adcs_data_t adcs = {
        .opmode = TEL_ADCS_OPMODE_DETUMBLE_ONLY,
        .flags = TEL_ADCS_FLAG_POINTING | TEL_ADCS_FLAG_TLE_LOADED,
        .filter_resets = 0x0102,
        .tle_epoch_unix_time = 0xCAFEF00D,
    };
    adcs.position[0] = -2.5f;
    adcs.covariance_diag[5] = 0.15625f;
    telemetry_update_adcs_data(&adcs);

    // A good capture, then a failed one, which must be what's sent
    telemetry_update_camera_data(SUCCESS, 1);
    telemetry_collect(&downlink);
    PVDX_ASSERT_MSG(downlink.camera.health == TEL_DEVICE_STATUS_OK && downlink.camera.last_capture_succeeded,
                    "camera OK after a capture\n");
    telemetry_update_camera_data(ERROR_SANITY_CHECK_FAILED, 2);

    // Bits 0, 9 and 12
    telemetry_update_state_inputs(&(input_t){.in_sunlight = true, .utc_valid = true, .safe_to_disable_radio = true});

    // Whether UTC is known depends on what ran before the tests, so the stamps are checked against utc.c either way
    uint32_t boot_time = 0;
    const bool utc_known = utc_get_boot_time(&boot_time) == SUCCESS;

    tel_collect_and_serialize(&downlink, buf);

    PVDX_ASSERT_MSG(buf[TEL_OFF_LAST_BOOTLOADER] == 1, "last bootloader\n");
    PVDX_ASSERT_MSG(buf[TEL_OFF_GYRO_HEALTH] == TEL_DEVICE_STATUS_BROKEN, "gyro broken after a failed read\n");
    PVDX_ASSERT_MSG(tel_be_float_is(&buf[TEL_OFF_GYRO_RATE], 1.0f) && tel_be_float_is(&buf[TEL_OFF_GYRO_RATE + 4], -2.5f) &&
                        tel_be_float_is(&buf[TEL_OFF_GYRO_RATE + 8], 0.15625f),
                    "gyro keeps the last good rates\n");
    PVDX_ASSERT_MSG(buf[TEL_OFF_DISPLAY_HEALTH] == TEL_DEVICE_STATUS_BROKEN, "display health\n");
    PVDX_ASSERT_MSG(buf[TEL_OFF_UHF_HEALTH] == TEL_DEVICE_STATUS_OK, "uhf health\n");

    ok = true;
    for (uint8_t pack = 0; pack < 4; pack++) {
        ok = ok && tel_be_float_is(&buf[TEL_OFF_EPS + 4 * pack], 25.0f * (pack + 1)) &&
            tel_be_float_is(&buf[TEL_OFF_EPS + 16 + 4 * pack], 3.5f + pack) &&
            tel_be_float_is(&buf[TEL_OFF_EPS + 32 + 4 * pack], -0.25f * (pack + 1)) &&
            tel_be_float_is(&buf[TEL_OFF_EPS + 48 + 4 * pack], 20.0f + pack);
    }
    PVDX_ASSERT_MSG(ok, "eps packs, and pack 4 dropped\n");

    ok = true;
    for (uint8_t i = 0; i < 8; i++) {
        ok = ok && buf[TEL_OFF_STATE_HISTORY + i] == 8 - i && tel_be32_is(&buf[TEL_OFF_TRANSITION_TIMES + 4 * i], 1008u - i);
    }
    PVDX_ASSERT_MSG(ok, "state history, newest first\n");

    ok = true;
    for (uint8_t i = 0; i < 16; i++) {
        ok = ok && tel_be32_is(&buf[TEL_OFF_COMMANDS + 4 * i], 0xC0000010u - i);
    }
    PVDX_ASSERT_MSG(ok, "command history, newest first\n");

    PVDX_ASSERT_MSG(
        buf[TEL_OFF_MRAM] == 0x39 && tel_be32_is(&buf[TEL_OFF_MRAM + 1], 3) && tel_be32_is(&buf[TEL_OFF_MRAM + 5 + 28], 0x01020304),
        "mram data\n");

    PVDX_ASSERT_MSG(tel_be32_is(&buf[TEL_OFF_MAG_RAW], 1) && tel_be32_is(&buf[TEL_OFF_MAG_RAW + 4], 0xFFFFFFFEu) &&
                        tel_be32_is(&buf[TEL_OFF_MAG_RAW + 8], 0x12345678) && tel_be_float_is(&buf[TEL_OFF_MAG_GAIN], 1.0f) &&
                        tel_be_float_is(&buf[TEL_OFF_MAG_GAIN + 4], -2.5f) && tel_be_float_is(&buf[TEL_OFF_MAG_GAIN + 8], 0.15625f),
                    "magnetometer readings\n");
    PVDX_ASSERT_MSG(tel_be_float_is(&buf[TEL_OFF_MAGNETORQUER], 0.5f) && tel_be_float_is(&buf[TEL_OFF_MAGNETORQUER + 4], -1.0f) &&
                        tel_be_float_is(&buf[TEL_OFF_MAGNETORQUER + 8], 0.25f),
                    "magnetorquer duty\n");

    ok = true;
    for (size_t i = 0; i < PHD_COUNT; i++) {
        ok = ok && buf[TEL_OFF_PHOTODIODE + 2 * i] == (uint8_t)(photodiodes[i] >> 8) &&
            buf[TEL_OFF_PHOTODIODE + 2 * i + 1] == (uint8_t)photodiodes[i];
    }
    PVDX_ASSERT_MSG(ok, "photodiode codes, in order\n");

    PVDX_ASSERT_MSG(buf[TEL_OFF_SBAND] == TEL_DEVICE_STATUS_BROKEN && tel_be32_is(&buf[TEL_OFF_SBAND + 1], 0xDEADBEEF), "sband data\n");

    // opmode 1, flags 0x11, filter resets 0x0102, TLE epoch, after the 15 floats
    static const uint8_t adcs_tail[] = {0x01, 0x11, 0x01, 0x02, 0xCA, 0xFE, 0xF0, 0x0D};
    PVDX_ASSERT_MSG(tel_be_float_is(&buf[TEL_OFF_ADCS], -2.5f) && tel_be_float_is(&buf[TEL_OFF_ADCS + 56], 0.15625f) &&
                        memcmp(&buf[TEL_OFF_ADCS + 60], adcs_tail, sizeof(adcs_tail)) == 0,
                    "adcs data\n");

    // 1.0, -2.5 and 0.15625 dps at 1600 counts per dps
    PVDX_ASSERT_MSG(tel_be32_is(&buf[TEL_OFF_GYRO_RAW], 1600) && tel_be32_is(&buf[TEL_OFF_GYRO_RAW + 4], (uint32_t)-4000) &&
                        tel_be32_is(&buf[TEL_OFF_GYRO_RAW + 8], 250),
                    "gyro raw counts from the rates\n");
    PVDX_ASSERT_MSG(
        buf[TEL_OFF_CAMERA] == TEL_DEVICE_STATUS_BROKEN && buf[TEL_OFF_CAMERA + 1] == TELEMETRY_FALSE && buf[TEL_OFF_CAMERA + 2] == 2,
        "camera broken after a failed capture\n");
    PVDX_ASSERT_MSG(buf[TEL_OFF_STATE_INPUTS] == 0x01 && buf[TEL_OFF_STATE_INPUTS + 1] == 0x12 && buf[TEL_OFF_STATE_INPUTS + 2] == 0x00,
                    "state inputs packed as bits\n");

    if (utc_known) {
        PVDX_ASSERT_MSG(tel_be32(&buf[TEL_OFF_BOOT_TIMESTAMP]) == boot_time && tel_be32(&buf[TEL_OFF_TIMESTAMP]) >= boot_time,
                        "timestamps stamped from UTC\n");
    } else {
        PVDX_ASSERT_MSG(tel_be32(&buf[TEL_OFF_TIMESTAMP]) == 0 && tel_be32(&buf[TEL_OFF_BOOT_TIMESTAMP]) == 0,
                        "timestamps 0 without UTC\n");
    }

    test_log("telemetry call site tests:\n");
    // An uplink with no commands ("PVDXGS", message size 0, 0 commands, timestamp 0x0A0B0C0D) records its timestamp;
    // one from another callsign doesn't. No commands, so nothing is enqueued before the scheduler starts
    static uint8_t uplink[] = {'P', 'V', 'D', 'X', 'G', 'S', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x0B, 0x0C, 0x0D};
    uplink_commands_dispatch(uplink, sizeof(uplink));
    uplink[0] = 'X';
    uplink[15] = 0xFF;
    uplink_commands_dispatch(uplink, sizeof(uplink));
    tel_collect_and_serialize(&downlink, buf);
    PVDX_ASSERT_MSG(tel_be32_is(&buf[TEL_OFF_COMMANDS], 0x0A0B0C0D) && tel_be32_is(&buf[TEL_OFF_COMMANDS + 4], 0xC0000010u),
                    "uplink timestamp recorded, rejected uplink not\n");

    // Re-running a device check publishes its result. Each starts from a value the check can never publish, so a check
    // that didn't publish fails. UHF isn't re-checked: that would reset the radio after the burn sequence set it up
    telemetry_update_mram_data(&(tel_mram_data_t){.status = 0xFF});
    telemetry_update_display_data(TEL_DEVICE_STATUS_DISABLED);
    uncheck_device(MRAM_ID);
    bool mram_okay = check_device(MRAM_ID);
    uncheck_device(DISPLAY_ID);
    bool display_okay = check_device(DISPLAY_ID);
    telemetry_update_magnetometer_registers(0xFF, 0xFF);
    uncheck_device(MAGNETOMETER_ID);
    bool magnetometer_okay = check_device(MAGNETOMETER_ID);
    tel_collect_and_serialize(&downlink, buf);
    // All three MRAMs OK is 0b010101: each chip's state in its own 2 bits
    PVDX_ASSERT_MSG(buf[TEL_OFF_MRAM] != 0xFF && (buf[TEL_OFF_MRAM] == 0x15) == mram_okay, "mram check publishes status\n");
    PVDX_ASSERT_MSG(buf[TEL_OFF_DISPLAY_HEALTH] == (display_okay ? TEL_DEVICE_STATUS_OK : TEL_DEVICE_STATUS_BROKEN),
                    "display check publishes health\n");
    PVDX_ASSERT_MSG(
        buf[TEL_OFF_LAST_BOOTLOADER + 1] == (magnetometer_okay ? RM3100_REVID_VALUE : 0) && buf[TEL_OFF_LAST_BOOTLOADER + 2] == 0,
        "magnetometer check publishes REVID\n");
}

void test_uplink_parse(void) {
    test_log("----- testing uplink parse -----\n");

    // Uplinks below were generated with Python's struct module, independently of uplink_parse.c.
    // Each is an uplink file as CFDP hands it over (no SPP headers), with a preamble with callsign "PVDXGS" and
    // timestamp 0x01020304

    // Seven commands: take IV curve with sampling resolution 64, kepler coefficients {97.5, 120.25, 2^-10, -45.5, 270,
    // 15.0625}, ADCS parameters (epoch JD 2461357.25, {3.75, -0.5, 0.15625, 1, -2.5, 15.0625}, B* 2^-13, rotation
    // {1, -2, 3}, flags 0xA8: flip x and z, dipole off), enable device 8 (display), reboot 1, send photo -2,
    // transmit on/off 0
    uint8_t multi[] = {0x50, 0x56, 0x44, 0x58, 0x47, 0x53, 0x00, 0x00, 0x00, 0x77, 0x00, 0x07, 0x01, 0x02, 0x03, 0x04, 0x00,
                       0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x40, 0x00, 0x08, 0x00, 0x00, 0x00, 0x18, 0x42, 0xC3,
                       0x00, 0x00, 0x42, 0xF0, 0x80, 0x00, 0x3A, 0x80, 0x00, 0x00, 0xC2, 0x36, 0x00, 0x00, 0x43, 0x87, 0x00,
                       0x00, 0x41, 0x71, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x28, 0x41, 0x42, 0xC7, 0x56, 0xA0, 0x00,
                       0x00, 0x00, 0x40, 0x70, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x00, 0x3E, 0x20, 0x00, 0x00, 0x3F, 0x80, 0x00,
                       0x00, 0xC0, 0x20, 0x00, 0x00, 0x41, 0x71, 0x00, 0x00, 0x39, 0x00, 0x00, 0x00, 0x01, 0xFE, 0x03, 0xA8,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x08, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00,
                       0x07, 0x00, 0x00, 0x00, 0x04, 0xFF, 0xFF, 0xFF, 0xFE, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00};
    static uplink_packet_t packet; // static so it doesn't use up the test's stack
    bool err;

    test_log("uplink parse test:\n");
    err = uplink_packet_parse(&packet, multi, sizeof(multi));
    PVDX_ASSERT_MSG(!err, "multi parse\n");
    PVDX_ASSERT_MSG(memcmp(packet.callsign, UPLINK_CALLSIGN, UPLINK_CALLSIGN_LENGTH) == 0, "multi callsign\n");
    PVDX_ASSERT_MSG(packet.message_size == sizeof(multi) - UPLINK_PREAMBLE_SIZE, "multi message_size excludes preamble\n");
    PVDX_ASSERT_MSG(packet.n_cmds == 7, "multi n_cmds\n");
    PVDX_ASSERT_MSG(packet.timestamp == 0x01020304, "multi timestamp\n");

    uplink_parsed_cmd_t *cmd = packet.cmds;
    PVDX_ASSERT_MSG(cmd[0].type == TAKE_IV_CURVE && cmd[0].args.iv_sampling_resolution == 64, "multi take IV curve\n");
    PVDX_ASSERT_MSG(cmd[0].data == &multi[22] && cmd[0].data_size == 4, "multi IV curve data points into the input\n");

    PVDX_ASSERT_MSG(cmd[1].type == SET_KEPLER_COEFF, "multi kepler type\n");
    PVDX_ASSERT_MSG(same_float(cmd[1].args.kepler_coefficients[0], 97.5f) && same_float(cmd[1].args.kepler_coefficients[1], 120.25f) &&
                        same_float(cmd[1].args.kepler_coefficients[2], 0.0009765625f) &&
                        same_float(cmd[1].args.kepler_coefficients[3], -45.5f) && same_float(cmd[1].args.kepler_coefficients[4], 270.0f) &&
                        same_float(cmd[1].args.kepler_coefficients[5], 15.0625f),
                    "multi kepler coefficients\n");

    uplink_adcs_parameters_t *adcs = &cmd[2].args.adcs_parameters;
    const double expected_epoch_jd = 2461357.25;
    PVDX_ASSERT_MSG(cmd[2].type == ADCS_UPDATE_PARAMS && memcmp(&adcs->epoch_jd, &expected_epoch_jd, sizeof(double)) == 0,
                    "multi adcs epoch\n");
    PVDX_ASSERT_MSG(same_float(adcs->inclination, 3.75f) && same_float(adcs->raan, -0.5f) && same_float(adcs->eccentricity, 0.15625f) &&
                        same_float(adcs->argument_of_perigee, 1.0f) && same_float(adcs->mean_anomaly, -2.5f) &&
                        same_float(adcs->mean_motion, 15.0625f) && same_float(adcs->bstar, 0.0001220703125f),
                    "multi adcs parameters\n");
    PVDX_ASSERT_MSG(adcs->device_rotation[0] == 1 && adcs->device_rotation[1] == -2 && adcs->device_rotation[2] == 3,
                    "multi adcs device rotation\n");
    PVDX_ASSERT_MSG(adcs->flip_magnetorquer_current[0] && !adcs->flip_magnetorquer_current[1] && adcs->flip_magnetorquer_current[2] &&
                        !adcs->bdot_override && adcs->dipole_off,
                    "multi adcs flags\n");

    PVDX_ASSERT_MSG(
        cmd[3].type == ENABLE_DISABLE_DEVICE && cmd[3].args.device_state.device == DISPLAY_ID && cmd[3].args.device_state.enabled,
        "multi enable/disable device\n");
    PVDX_ASSERT_MSG(cmd[4].type == REBOOT && cmd[4].args.reboot == 1, "multi reboot\n");
    PVDX_ASSERT_MSG(cmd[5].type == SEND_PHOTO && cmd[5].args.photo_count == -2, "multi send photo (negative)\n");
    PVDX_ASSERT_MSG(cmd[6].type == TRANSMIT_ON_OFF && cmd[6].data_size == 1 && cmd[6].args.transmit == 0, "multi transmit on/off\n");

    // Bytes after the end given by message_size are ignored
    uint8_t padded[sizeof(multi) + 4] = {0};
    memcpy(padded, multi, sizeof(multi));
    PVDX_ASSERT_MSG(!uplink_packet_parse(&packet, padded, sizeof(padded)), "trailing bytes accepted\n");

    // No commands at all
    uint8_t empty[] = {0x50, 0x56, 0x44, 0x58, 0x47, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04};
    err = uplink_packet_parse(&packet, empty, sizeof(empty));
    PVDX_ASSERT_MSG(!err && packet.n_cmds == 0, "no commands\n");

    // One set unix time command, 1795000000
    uint8_t unix_time[] = {0x50, 0x56, 0x44, 0x58, 0x47, 0x53, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x01, 0x01,
                           0x02, 0x03, 0x04, 0x00, 0x09, 0x00, 0x00, 0x00, 0x04, 0x6A, 0xFD, 0x86, 0xC0};
    err = uplink_packet_parse(&packet, unix_time, sizeof(unix_time));
    PVDX_ASSERT_MSG(!err && packet.cmds[0].type == SET_UNIX_TIME && packet.cmds[0].args.unix_time == 1795000000u, "unix time\n");

    // One set ADCS opmode command, detumble only (1)
    uint8_t opmode[] = {0x50, 0x56, 0x44, 0x58, 0x47, 0x53, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01,
                        0x01, 0x02, 0x03, 0x04, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01};
    err = uplink_packet_parse(&packet, opmode, sizeof(opmode));
    PVDX_ASSERT_MSG(!err && packet.cmds[0].type == SET_ADCS_OP_MODE && packet.cmds[0].args.adcs_op_mode == TEL_ADCS_OPMODE_DETUMBLE_ONLY,
                    "adcs opmode\n");
    opmode[sizeof(opmode) - 1] = TEL_ADCS_OPMODE_COUNT;
    PVDX_ASSERT_MSG(uplink_packet_parse(&packet, opmode, sizeof(opmode)), "unknown adcs opmode rejected\n");

    // One send IV curve command, whose one byte of data has no meaning
    uint8_t send_iv[] = {0x50, 0x56, 0x44, 0x58, 0x47, 0x53, 0x00, 0x00, 0x00, 0x07, 0x00, 0x01,
                         0x01, 0x02, 0x03, 0x04, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00};
    err = uplink_packet_parse(&packet, send_iv, sizeof(send_iv));
    PVDX_ASSERT_MSG(!err && packet.cmds[0].type == SEND_IV_CURVE && packet.cmds[0].data_size == 1, "send IV curve\n");

    // One update display command with an 8192-byte bitmap. Only the headers were generated; the bitmap is filled in here
    test_log("uplink display test:\n");
    uint8_t display_header[] = {0x50, 0x56, 0x44, 0x58, 0x47, 0x53, 0x00, 0x00, 0x20, 0x06, 0x00,
                                0x01, 0x01, 0x02, 0x03, 0x04, 0x00, 0x01, 0x00, 0x00, 0x20, 0x00};
    static uint8_t display[sizeof(display_header) + UPLINK_DISPLAY_BITMAP_SIZE];
    memcpy(display, display_header, sizeof(display_header));
    for (size_t i = sizeof(display_header); i < sizeof(display); i++) {
        display[i] = (uint8_t)(i * 7);
    }
    PVDX_ASSERT_MSG(sizeof(display) <= CFDP_FILE_BUFF_SZ, "display uplink fits in the CFDP file buffer\n");
    err = uplink_packet_parse(&packet, display, sizeof(display));
    PVDX_ASSERT_MSG(!err, "display parse\n");
    PVDX_ASSERT_MSG(packet.cmds[0].type == UPDATE_DISPLAY, "display type\n");
    PVDX_ASSERT_MSG(packet.cmds[0].data == &display[sizeof(display_header)], "display bitmap points into the input\n");
    PVDX_ASSERT_MSG(packet.cmds[0].data_size == UPLINK_DISPLAY_BITMAP_SIZE, "display bitmap size\n");

    test_log("uplink rejection tests:\n");
    uint8_t bad[sizeof(multi)];

    PVDX_ASSERT_MSG(uplink_packet_parse(&packet, multi, sizeof(multi) - 1), "shorter than message size\n");
    PVDX_ASSERT_MSG(uplink_packet_parse(NULL, multi, sizeof(multi)) && uplink_packet_parse(&packet, NULL, sizeof(multi)), "NULL args\n");

    memcpy(bad, multi, sizeof(multi));
    bad[5] = 'X'; // last callsign character, which the old parser skipped
    PVDX_ASSERT_MSG(uplink_packet_parse(&packet, bad, sizeof(bad)), "wrong callsign rejected\n");

    memcpy(bad, multi, sizeof(multi));
    bad[9] += 1; // message size one too big
    PVDX_ASSERT_MSG(uplink_packet_parse(&packet, bad, sizeof(bad)), "message size past the end rejected\n");

    memcpy(bad, multi, sizeof(multi));
    bad[11] = 8; // one more command than there is
    PVDX_ASSERT_MSG(uplink_packet_parse(&packet, bad, sizeof(bad)), "too many commands for uplink rejected\n");

    memcpy(bad, multi, sizeof(multi));
    bad[11] = 6; // one less command than there is, so the last one is left over
    PVDX_ASSERT_MSG(uplink_packet_parse(&packet, bad, sizeof(bad)), "left over bytes rejected\n");

    memcpy(bad, multi, sizeof(multi));
    bad[11] = UPLINK_MAX_CMDS + 1;
    PVDX_ASSERT_MSG(uplink_packet_parse(&packet, bad, sizeof(bad)), "more than max commands rejected\n");

    memcpy(bad, multi, sizeof(multi));
    bad[17] = UPLINK_CMD_COUNT; // take IV curve command becomes an unknown command
    PVDX_ASSERT_MSG(uplink_packet_parse(&packet, bad, sizeof(bad)), "unknown command rejected\n");

    // Preamble cut off after the message size
    uint8_t short_preamble[] = {0x50, 0x56, 0x44, 0x58, 0x47, 0x53, 0x00, 0x00, 0x00, 0x00};
    PVDX_ASSERT_MSG(uplink_packet_parse(&packet, short_preamble, sizeof(short_preamble)), "short preamble rejected\n");

    // Take IV curve command with 2 bytes of data instead of 4
    uint8_t bad_size[] = {0x50, 0x56, 0x44, 0x58, 0x47, 0x53, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01,
                          0x01, 0x02, 0x03, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x40};
    PVDX_ASSERT_MSG(uplink_packet_parse(&packet, bad_size, sizeof(bad_size)), "wrong command data size rejected\n");

    // ADCS parameters with flags 0x14: B-dot override, plus reserved bit 2
    uint8_t adcs_reserved[] = {0x50, 0x56, 0x44, 0x58, 0x47, 0x53, 0x00, 0x00, 0x00, 0x2E, 0x00, 0x01, 0x01, 0x02, 0x03, 0x04,
                               0x00, 0x0B, 0x00, 0x00, 0x00, 0x28, 0x41, 0x42, 0xC7, 0x56, 0xA0, 0x00, 0x00, 0x00, 0x40, 0x70,
                               0x00, 0x00, 0xBF, 0x00, 0x00, 0x00, 0x3E, 0x20, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0xC0, 0x20,
                               0x00, 0x00, 0x41, 0x71, 0x00, 0x00, 0x39, 0x00, 0x00, 0x00, 0x01, 0xFE, 0x03, 0x14};
    PVDX_ASSERT_MSG(uplink_packet_parse(&packet, adcs_reserved, sizeof(adcs_reserved)), "adcs reserved flag bits rejected\n");
    adcs_reserved[sizeof(adcs_reserved) - 1] = 0x10; // B-dot override only
    err = uplink_packet_parse(&packet, adcs_reserved, sizeof(adcs_reserved));
    PVDX_ASSERT_MSG(!err && packet.cmds[0].args.adcs_parameters.bdot_override, "adcs B-dot override\n");

    // Enable device 12, which is NUM_DEVICES
    uint8_t bad_device[] = {0x50, 0x56, 0x44, 0x58, 0x47, 0x53, 0x00, 0x00, 0x00, 0x09, 0x00, 0x01, 0x01,
                            0x02, 0x03, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x0C, 0x01};
    PVDX_ASSERT_MSG(uplink_packet_parse(&packet, bad_device, sizeof(bad_device)), "unknown device rejected\n");

    // Device 8 (display) set to state 2, which is neither enabled nor disabled
    uint8_t bad_device_state[] = {0x50, 0x56, 0x44, 0x58, 0x47, 0x53, 0x00, 0x00, 0x00, 0x09, 0x00, 0x01, 0x01,
                                  0x02, 0x03, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x08, 0x02};
    PVDX_ASSERT_MSG(uplink_packet_parse(&packet, bad_device_state, sizeof(bad_device_state)), "unknown device state rejected\n");
}

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

// On-board test of the PMB measurement chain: ADG734 pixel routing (via the MCP23017), PVD_CRNT DAC and INA226.
// First a sanity stage checks that every device is present, initializes and responds correctly; only if it
// passes, every pixel is swept from 0 to full scale and the INA226 bus voltage is checked against the commanded
// value. Requires the PMB hardware; results are reported over RTT.

#define PMB_TEST_STEP_MV 50U                                              // Pixel voltage step
#define PMB_TEST_TOLERANCE_MV 50                                          // Allowed |measured - commanded| pixel voltage
#define PMB_TEST_SETTLE_MS 5U                                             // Opamp settling time after a DAC step
#define PMB_TEST_CONVERSION_TIMEOUT_MS (4U * INA226_CONVERSION_PERIOD_MS) // Two conversions plus margin
#define PMB_TEST_CHAIN_MIN_SWING_MV (PVD_PIXEL_FULL_SCALE_MV / 2)         // Minimum response to a 0 -> full-scale step

static void test_pmb_sweep_pixel(uint8_t pixel) {
    PVDX_ASSERT_MSG(adg734_select_pixel(pixel) == SUCCESS, "pmb select pixel\n");
    uint8_t selected = PMB_PIXEL_NONE;
    PVDX_ASSERT_MSG(adg734_read_selected(&selected) == SUCCESS && selected == pixel, "pmb pixel select readback\n");

    int out_of_tolerance = 0;
    for (uint32_t target_mv = 0; target_mv <= PVD_PIXEL_FULL_SCALE_MV; target_mv += PMB_TEST_STEP_MV) {
        watchdog_feed(WDT);

        ina226_measurement_t m = {0};
        status_t status = pvd_dac_set_pixel_mv(target_mv);
        if (status == SUCCESS) {
            os_delay_ms(PMB_TEST_SETTLE_MS);
            status = ina226_wait_for_fresh_conversion(PMB_TEST_CONVERSION_TIMEOUT_MS);
        }
        if (status == SUCCESS) {
            status = ina226_read_measurement(&m);
        }
        PVDX_ASSERT_MSG(status == SUCCESS, "pmb sweep step comms\n");
        if (status != SUCCESS) {
            test_log("px %2u  set %4u mV  status %d\n", pixel, (unsigned)target_mv, status);
            continue;
        }

        // The opamp regulates FOLLOW, so that is what must match the command; the pixel differs by the shunt drop
        int follow_mv = (int)(m.follow_uv / 1000);
        int pixel_mv = (int)(m.bus_uv / 1000U);
        int error_mv = follow_mv - (int)target_mv;
        test_log("px %2u  set %4u mV  follow %4d mV  err %4d mV  pixel %4d mV  I %8d nA  P %7u nW\n", pixel, (unsigned)target_mv, follow_mv,
                 error_mv, pixel_mv, (int)m.current_na, (unsigned)m.power_nw);

        bool in_tolerance = error_mv <= PMB_TEST_TOLERANCE_MV && error_mv >= -PMB_TEST_TOLERANCE_MV;
        PVDX_ASSERT_MSG(in_tolerance, "pmb opamp output tracks DAC\n");
        if (!in_tolerance) {
            out_of_tolerance++;
        }
    }
    test_log("px %2u  done, %d steps out of tolerance\n", pixel, out_of_tolerance);
}

// Settles, waits for a conversion that reflects the current input, and reads the opamp output voltage (-1 on failure)
static status_t test_pmb_measure_mv(int *measured_mv) {
    os_delay_ms(PMB_TEST_SETTLE_MS);
    status_t status = ina226_wait_for_fresh_conversion(PMB_TEST_CONVERSION_TIMEOUT_MS);
    ina226_measurement_t m = {0};
    if (status == SUCCESS) {
        status = ina226_read_measurement(&m);
    }
    *measured_mv = (status == SUCCESS) ? (int)(m.follow_uv / 1000) : -1;
    return status;
}

// Lists every address that acknowledges on the PMB bus; anything other than the INA226 and MCP23017 is unexpected
static void test_pmb_scan_bus(void) {
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (pmb_i2c_probe(addr) == SUCCESS) {
            test_log("pmb: device at 0x%02x%s\n", addr,
                     (addr == INA226_I2C_ADDR)         ? " (INA226)"
                         : (addr == MCP23017_I2C_ADDR) ? " (MCP23017)"
                                                       : " (unexpected)");
        }
    }
}

/**
 * Checks each device and link on its own, in dependency order, so a failure points at one part:
 * I2C bus -> INA226 -> MCP23017 -> ADG734 routing -> DAC -> DAC-to-INA226 measurement chain.
 * Returns the number of failed checks.
 */
static int test_pmb_sanity(void) {
    int failed_before = tests_total - tests_passed;
    test_log("--- pmb sanity checks ---\n");

    // I2C bus and device presence
    test_pmb_scan_bus();
    PVDX_ASSERT_MSG(pmb_i2c_probe(INA226_I2C_ADDR) == SUCCESS, "pmb ina226 acks at 0x40\n");
    PVDX_ASSERT_MSG(pmb_i2c_probe(MCP23017_I2C_ADDR) == SUCCESS, "pmb mcp23017 acks at 0x20\n");

    // INA226: identity, init, register readback and a live conversion
    uint16_t value = 0;
    PVDX_ASSERT_MSG(ina226_read_reg(INA226_MANUFACTURER_ID, &value) == SUCCESS && value == INA226_MFG_ID, "pmb ina226 manufacturer id\n");
    PVDX_ASSERT_MSG(ina226_read_reg(INA226_DIE_ID, &value) == SUCCESS && (value & 0xFFF0U) == INA226_DIE_ID_VALUE, "pmb ina226 die id\n");
    PVDX_ASSERT_MSG(ina226_init() == SUCCESS, "pmb ina226 init\n");
    PVDX_ASSERT_MSG(ina226_read_reg(INA226_CONFIG, &value) == SUCCESS && value == INA226_DEFAULT_CONFIG, "pmb ina226 config readback\n");
    PVDX_ASSERT_MSG(ina226_read_reg(INA226_CALIBRATION, &value) == SUCCESS && value == INA226_CAL_VALUE,
                    "pmb ina226 calibration readback\n");
    PVDX_ASSERT_MSG(ina226_wait_for_fresh_conversion(PMB_TEST_CONVERSION_TIMEOUT_MS) == SUCCESS, "pmb ina226 converting\n");

    // ADG734 IN lines float until the MCP23017 drives them, so adg734_init() (which also brings up the MCP23017)
    // runs before anything else touches the expander
    PVDX_ASSERT_MSG(adg734_init() == SUCCESS, "pmb adg734 init\n");

    // MCP23017: a write/readback pattern on DEFVALA/B (inert while GPINTEN = 0) to exercise both directions
    const uint16_t patterns[] = {0xA55AU, 0x5AA5U, 0x0000U};
    for (unsigned i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        uint16_t readback = (uint16_t)~patterns[i];
        PVDX_ASSERT_MSG(mcp23017_write_reg16(MCP23017_DEFVALA, patterns[i]) == SUCCESS &&
                            mcp23017_read_reg16(MCP23017_DEFVALA, &readback) == SUCCESS && readback == patterns[i],
                        "pmb mcp23017 register write/readback\n");
    }

    // ADG734 routing: all IN pins outputs, nothing selected, then each pixel alone. Selection is read from the
    // MCP23017 pin levels, so a stuck or shorted IN line shows up as the wrong pixel or two pixels at once (the
    // switches themselves are only proven by current through the pixel).
    uint16_t iodir = 0xFFFF;
    PVDX_ASSERT_MSG(mcp23017_read_reg16(MCP23017_IODIRA, &iodir) == SUCCESS && iodir == MCP23017_ALL_OUTPUTS,
                    "pmb adg734 IN pins are outputs\n");
    uint8_t selected = 0;
    PVDX_ASSERT_MSG(adg734_read_selected(&selected) == SUCCESS && selected == PMB_PIXEL_NONE, "pmb no pixel selected after init\n");
    for (uint8_t pixel = 0; pixel < PMB_PIXEL_COUNT; pixel++) {
        selected = PMB_PIXEL_NONE;
        bool ok = adg734_select_pixel(pixel) == SUCCESS && adg734_read_selected(&selected) == SUCCESS && selected == pixel;
        if (!ok) {
            test_log("pmb: selecting pixel %u read back %u\n", pixel, selected);
        }
        PVDX_ASSERT_MSG(ok, "pmb adg734 select each pixel\n");
    }
    PVDX_ASSERT_MSG(adg734_deselect_all() == SUCCESS && adg734_read_selected(&selected) == SUCCESS && selected == PMB_PIXEL_NONE,
                    "pmb adg734 deselect all\n");

    // DAC: channel up and accepting writes
    PVDX_ASSERT_MSG(pvd_dac_init() == SUCCESS, "pmb dac init\n");
    PVDX_ASSERT_MSG(pvd_dac_is_ready(), "pmb dac ready\n");

    // Measurement chain: at 0 V the opamp output should sit near 0; a full-scale step on pixel 0 must move it by at
    // least half of full scale (DAC -> divider -> opamp -> shunt -> INA226)
    int zero_mv = -1, full_mv = -1;
    PVDX_ASSERT_MSG(pvd_dac_set_pixel_mv(0) == SUCCESS && test_pmb_measure_mv(&zero_mv) == SUCCESS, "pmb chain read at 0 V\n");
    PVDX_ASSERT_MSG(zero_mv >= 0 && zero_mv <= PMB_TEST_TOLERANCE_MV, "pmb chain idle voltage near 0\n");
    PVDX_ASSERT_MSG(adg734_select_pixel(0) == SUCCESS, "pmb chain select pixel 0\n");
    PVDX_ASSERT_MSG(pvd_dac_set_pixel_mv(PVD_PIXEL_FULL_SCALE_MV) == SUCCESS && test_pmb_measure_mv(&full_mv) == SUCCESS,
                    "pmb chain read at full scale\n");
    PVDX_ASSERT_MSG(full_mv - zero_mv >= (int)PMB_TEST_CHAIN_MIN_SWING_MV, "pmb chain responds to DAC\n");
    test_log("pmb: chain 0 V -> %d mV, full scale -> %d mV\n", zero_mv, full_mv);
    pvd_dac_set_code(0);
    adg734_deselect_all();

    int failures = (tests_total - tests_passed) - failed_before;
    test_log("--- pmb sanity: %d check(s) failed ---\n", failures);
    return failures;
}

void test_pmb(void) {
    test_log("----- testing pmb (on hardware) -----\n");

    if (test_pmb_sanity() != 0) {
        test_log("pmb: sanity checks failed, skipping sweep\n");
        pvd_dac_set_code(0);
        adg734_deselect_all();
        return;
    }

    for (uint8_t pixel = 0; pixel < PMB_PIXEL_COUNT; pixel++) {
        test_pmb_sweep_pixel(pixel);
    }

    // Leave the circuit idle
    PVDX_ASSERT_MSG(pvd_dac_set_code(0) == SUCCESS, "pmb dac to 0\n");
    PVDX_ASSERT_MSG(adg734_deselect_all() == SUCCESS, "pmb deselect all\n");
}

#endif // UNITTEST
