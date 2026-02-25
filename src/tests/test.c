
#include "tests/test.h"

#include "logging.h"
#include "radio/cfdp_pdu.h"
#include "radio/spp.h"
#include "utils_assert.h"
void test_spp(void);
void test_cfdp(void);

void tests_run(void) {
    test_spp();
    test_cfdp();
}

#ifdef UNITTEST
void test_spp(void) {
    spp_packet_t packet = spp_packet_create_header_only(0xBB, 0, 1, 0b10, 0b0011111111111111, 0xAA);
    test_log("----- testing spp -----\n");
    test_log("spp packet test:\n");
    test_log("header size: %d\n", sizeof(packet.header));
    test_log("full packet size (max w/ internal buffer): %d\n", sizeof(packet));

    test_log("apid: %x\n", packet.header.application_process_id);
    ASSERT(packet.header.application_process_id == 0xBB && "apid\n");

    test_log("secondary_header_flag: %x\n", packet.header.secondary_header_flag);
    ASSERT(packet.header.secondary_header_flag == 0 && "secondary_header_flag\n");

    test_log("packet_type: %x\n", packet.header.packet_type);
    ASSERT(packet.header.packet_type == 1 && "packet_type\n");

    test_log("sequence_flags: %x\n", packet.header.sequence_flags);
    ASSERT(packet.header.sequence_flags == 0b10 && "sequence_flags\n");

    test_log("packet_seq_count_or_name: %x\n", packet.header.sequence_count);
    ASSERT(packet.header.sequence_count == 0b0011111111111111 && "packet_seq_count_or_name\n");

    test_log("data_length: %x\n", packet.header.data_length);
    ASSERT(packet.header.data_length == 0xAA && "data_length");
}

void test_cfdp(void) {
    test_log("----- testing cfdp -----\n");

    test_log("cfdp header parse test:\n");
    uint8_t raw_header[] = {0x20, 0x00, 0x0A, 0x00, 0x01, 0x05, 0x02};
    cfdp_pdu_header_t header;
    int ret = cfdp_pdu_header_parse(raw_header, sizeof(raw_header), &header);

    test_log("return value: %d\n", ret);
    ASSERT(ret == 7 && "header parse return");

    test_log("version_number: %d\n", header.version_number);
    ASSERT(header.version_number == CFDP_VERSION_NUMBER && "version_number");

    test_log("pdu_type: %d\n", header.pdu_type);
    ASSERT(header.pdu_type == CFDP_PDU_TYPE_DIRECTIVE && "pdu_type");

    test_log("pdu_data_length: %d\n", header.pdu_data_length);
    ASSERT(header.pdu_data_length == 10 && "pdu_data_length");

    test_log("entity_id_len: %d\n", header.entity_id_len);
    ASSERT(header.entity_id_len == 1 && "entity_id_len");

    test_log("source_entity_id: 0x%02x\n", header.source_entity_id.data[0]);
    ASSERT(header.source_entity_id.data[0] == 0x01 && "source_entity_id");

    test_log("transaction_seq: 0x%02x\n", header.transaction_seq.data[0]);
    ASSERT(header.transaction_seq.data[0] == 0x05 && "transaction_seq");

    test_log("dest_entity_id: 0x%02x\n", header.dest_entity_id.data[0]);
    ASSERT(header.dest_entity_id.data[0] == 0x02 && "dest_entity_id");

    test_log("cfdp header parse error (too short) test:\n");
    ret = cfdp_pdu_header_parse(raw_header, 3, &header);
    ASSERT(ret == -1 && "header parse too short");

    test_log("cfdp metadata parse test:\n");
    uint8_t raw_metadata[] = {0x40, 0x00, 0x00, 0x01, 0x00, 0x01, 0x0A, 0x01, 0x0B};
    cfdp_pdu_metadata_t metadata;
    ret = cfdp_pdu_metadata_parse(raw_metadata, sizeof(raw_metadata), &metadata);

    test_log("return value: %d\n", ret);
    ASSERT(ret == 9 && "metadata parse return");

    test_log("closure_req: %d\n", metadata.closure_req);
    ASSERT(metadata.closure_req == 1 && "closure_req");

    test_log("checksum_type: %d\n", metadata.checksum_type);
    ASSERT(metadata.checksum_type == 0 && "checksum_type");

    test_log("file_length: %u\n", metadata.file_length);
    ASSERT(metadata.file_length == 256 && "file_length");

    test_log("source_id: 0x%02x\n", metadata.source_id);
    ASSERT(metadata.source_id == 0x0A && "source_id");

    test_log("dest_id: 0x%02x\n", metadata.dest_id);
    ASSERT(metadata.dest_id == 0x0B && "dest_id");

    test_log("options.len: %d\n", metadata.options.len);
    ASSERT(metadata.options.len == 0 && "metadata options empty");

    test_log("cfdp eof parse test:\n");
    uint8_t raw_eof[] = {0x00, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x01, 0x00};
    cfdp_pdu_eof_t eof;
    ret = cfdp_pdu_eof_parse(raw_eof, sizeof(raw_eof), false, &eof);

    test_log("return value: %d\n", ret);
    ASSERT(ret == 9 && "eof parse return");

    test_log("condition_code: %d\n", eof.condition_code);
    ASSERT(eof.condition_code == CFDP_COND_NOERROR && "condition_code");

    test_log("checksum: 0x%08x\n", eof.checksum);
    ASSERT(eof.checksum == 0xDEADBEEF && "checksum");

    test_log("filesize: %u\n", eof.filesize);
    ASSERT(eof.filesize == 256 && "filesize");

    test_log("fault_entity_id.len: %d\n", eof.fault_entity_id.len);
    ASSERT(eof.fault_entity_id.len == 0 && "fault_entity_id empty");

    test_log("cfdp filedata parse test:\n");
    uint8_t raw_filedata[] = {0x00, 0x00, 0x04, 0x00, 0xCA, 0xFE, 0xBA, 0xBE};
    cfdp_pdu_filedata_t filedata;
    ret = cfdp_pdu_filedata_parse(raw_filedata, sizeof(raw_filedata), false, false, &filedata);

    test_log("return value: %d\n", ret);
    ASSERT(ret == 8 && "filedata parse return");

    test_log("offset: %u\n", filedata.offset);
    ASSERT(filedata.offset == 1024 && "offset");

    test_log("data.len: %d\n", filedata.data.len);
    ASSERT(filedata.data.len == 4 && "data length");

    test_log("data[0]: 0x%02x\n", filedata.data.data[0]);
    ASSERT(filedata.data.data[0] == 0xCA && "data[0]");

    test_log("data[1]: 0x%02x\n", filedata.data.data[1]);
    ASSERT(filedata.data.data[1] == 0xFE && "data[1]");
}
#endif
