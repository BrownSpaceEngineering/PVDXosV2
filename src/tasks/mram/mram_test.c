#include "mram_driver.h"

#define MRAM_TEST_POS   0x55
#define MRAM_TEST_LEN   0x10
#define MRAM_TEST_DEF   0xaa

void mram_test_cmp(char *write_buf, char *read_buf) {
    char any_neq = 0;
    for (char i = 0; i < MRAM_TEST_LEN; i++) {
        if (write_buf[i] != read_buf[i]) {
            any_neq = 1;
            break;
        }
    }
    if (!any_neq) {
        return;
    }

    warning("write buf:");
    for (char i = 0; i < MRAM_TEST_LEN; i++) {
        warning(" %d", write_buf[i]);
    }
    warning("\nread buf:");
    for (char i = 0; i < MRAM_TEST_LEN; i++) {
        warning(" %d", read_buf[i]);
    }
    warning("\n(default read buf value: %d)\n", MRAM_TEST_DEF);
    fatal("mram test failed");
}

void mram_test(void) {
    massert(0 < MRAM_TEST_LEN && MRAM_TEST_LEN < 256);

    char write_buf[MRAM_TEST_LEN], read_buf[MRAM_TEST_LEN];
    for (char i = 0; i < MRAM_TEST_LEN; i++) {
        write_buf[i] = i;
    }

    mram_init();

    memset(read_buf, MRAM_TEST_DEF, MRAM_TEST_LEN);
    mram_write_raw(MRAM_TEST_POS, MRAM_TEST_LEN, write_buf);
    mram_read_raw(MRAM_TEST_POS, MRAM_TEST_LEN, read_buf);
    massert(memcmp(write_buf, read_buf, MRAM_TEST_LEN) == 0);

    memset(read_buf, MRAM_TEST_DEF, MRAM_TEST_LEN);
    mram_write(MRAM_TEST_POS, MRAM_TEST_LEN, write_buf);
    mram_read(MRAM_TEST_POS, MRAM_TEST_LEN, read_buf);
    massert(memcmp(write_buf, read_buf, MRAM_TEST_LEN) == 0);
}