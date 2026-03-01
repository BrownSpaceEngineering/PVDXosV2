
#include "tests/test.h"

#include "logging.h"
#include "radio/spp.h"
#include "utils_assert.h"
void test_spp(void);

void tests_run(void) {
    test_spp();
    test_matrix_product();
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
#endif
