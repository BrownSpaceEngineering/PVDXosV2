
#include "tests/test.h"

#include "linalg/LinearAlgebra/declareFunctions.h"
#include "logging.h"
#include "radio/spp.h"
#include "utils_assert.h"

void test_spp(void);
void test_matrix_product(void);

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
    double A[4] = {1., 2., 3., 4.};
    double B[4] = {5., 6., 7., 8.};
    double C[4] = {0.};
    double C_expected[4] = {19., 22., 43., 50.};

    mul(A, B, false, C, 2, 2, 2);
    // log_matrix(C, 2, 2);

    if (dbl_eps_close_matrix(C, C_expected, 2, 2, DBL_EPSILON)) {
        test_log("1 * 2 matrix product test passed!\n");
    } else {
        test_log("2 * 2 matrix product test failed!\n");
    }

    // 4*4 identity matrix test
    double identity[4 * 4] = {0.};
    double large_result[4 * 4] = {0.};

    eye(identity, 4, 4);

    mul(identity, identity, false, large_result, 4, 4, 4);

    if (dbl_eps_close_matrix(identity, large_result, 4, 4, DBL_EPSILON)) {
        test_log("Identity matrix squared test passed!\n");
    } else {
        test_log("Identity matrix squared test failed!\n");
    }

    double A_large[4 * 4] = {1., 2., 3., 4., 5., 6., 7., 8., 9., 10., 11., 12., 13., 14., 15., 16.};

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

    double B_large[4 * 4] = {5.24829, 6.21496, 3.27374, 3.49223, 1.52040, 3.70849, 7.21884, 0.41667,
                             7.77438, 8.24807, 8.63347, 2.01096, 8.29170, 1.46735, 8.53606, 5.14221};

    double C_large[4 * 4] = {1.92304, 0.14043, 1.64762, 4.97396, 0.68077, 4.99275, 7.04041, 2.44857,
                             8.22049, 1.66745, 7.94150, 0.56302, 2.68638, 7.59450, 1.43236, 3.59834};

    double large_multiplication_expected[4 * 4] = {50.6168, 63.7473, 83.4036, 55.732,  65.9102, 33.9305, 86.5396, 22.2066,
                                                   96.939,  71.9404, 142.322, 70.9624, 100.929, 61.7765, 99.1469, 68.1449};

    mul(B_large, C_large, false, large_result, 4, 4, 4);

    if (dbl_eps_close_matrix(large_result, large_multiplication_expected, 4, 4, DBL_EPSILON)) {
        test_log("Large matrix product test passed!\n");
    } else {
        test_log("Large matrix product test failed!\n");
    }
}
#endif
