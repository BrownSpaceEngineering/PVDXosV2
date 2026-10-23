/**
 * test_stxg2.c
 *
 * On-board test of the STXG2 S-band transmitter. Powers the radio on and reads its identity and health registers. If they are
 * good, it transmits at the lowest power and MODCOD 1. STXG2_TEST_PATTERN selects the data:
 *  - STXG2_TEST_PATTERN_TPGEN: Generated sequence from the STXG2's TPGEN. Should not be transmitted over a full link.
 *  - STXG2_TEST_PATTERN_CALLSIGN: Repeats "DE <callsign> PVDX TEST " followed by a PRBS7 sequence. The packet is
 *    stored in the radio's flash over SPI and then sent again and again.
 * Requires the STXG2 hardware with an antenna or a 50 ohm load on the RF port. Results can be obtained over RTT.
 */

#include "tests/test_stxg2.h"

#include <string.h>

#include "drivers/stxg2/stxg2.h"
#include "drivers/watchdog/watchdog_driver.h"
#include "hal_delay.h"
#include "logging.h"
#include "tests/test.h"

#define STXG2_TEST_PATTERN_TPGEN 1
#define STXG2_TEST_PATTERN_CALLSIGN 0

// Pick the data to transmit
#ifndef STXG2_TEST_PATTERN
    #define STXG2_TEST_PATTERN STXG2_TEST_PATTERN_TPGEN
#endif

// The callsign must be a string literal. It is deliberately not defaulted: an on-air ID must be the operator's own.
// For example: #define STXG2_TEST_CALLSIGN "W1AW"
#if STXG2_TEST_PATTERN == STXG2_TEST_PATTERN_CALLSIGN && !defined(STXG2_TEST_CALLSIGN)
    #error "Define STXG2_TEST_CALLSIGN as a string literal (your amateur callsign) to use STXG2_TEST_PATTERN_CALLSIGN"
#endif

#define STXG2_TEST_FREQ_KHZ 2418000U
#define STXG2_TEST_SYM_RATE_KSPS STXG2_SYM_RATE_MIN_KSPS
#define STXG2_TEST_MODCOD 1U           // Lowest MODCOD the radio accepts (USM 9.2.5.5)
#define STXG2_TEST_LOCK_TIMEOUT_MS 2000U
#define STXG2_TEST_REPORT_PERIOD_MS 1000U
#define STXG2_TEST_MEM_INDEX 0U             // Flash packet that holds the callsign pattern
#define STXG2_TEST_PRBS_BYTES 64U           // PRBS7 bytes after each ID text
#define STXG2_TEST_PACKET_TIMEOUT_MS 10000U // Generous: one packet takes well under a second at 1 Msps

// BOARD_STAT flags (USM 9.2.4.1) that mean a fault. The I2C and RF lock flags are checked separately.
#define BOARD_STATUS_MEM (1u << 3)
#define BOARD_STATUS_PA (1u << 5)
#define BOARD_STATUS_PA_WARN (1u << 6)
#define BOARD_STATUS_TIMEOUT (1u << 7)
#define BOARD_STATUS_BRD_WARN (1u << 8)
#define BOARD_STATUS_BRD_TRIP (1u << 9)
#define BOARD_STATUS_BRD_CURR (1u << 10)
#define BOARD_STATUS_BRD_VOLT (1u << 11)
#define BOARD_STATUS_FAULT_MSK                                                                                                             \
    (BOARD_STATUS_MEM | BOARD_STATUS_PA | BOARD_STATUS_PA_WARN | BOARD_STATUS_TIMEOUT | BOARD_STATUS_BRD_WARN | BOARD_STATUS_BRD_TRIP |    \
     BOARD_STATUS_BRD_CURR | BOARD_STATUS_BRD_VOLT)

// TP_GEN_CTRL (USM 9.2.8.8). Pattern 0 = 8-bit counter; length 0 = generate until disabled.
#define TP_GEN_CTRL_ENABLE (1u << 0)

// PA_TIMEOUT (USM 9.2.9.4). A limit of 0 turns the 18 minute default timeout off.
#define PA_TIMEOUT_NO_LIMIT 0u

// Asserts like PVDX_ASSERT_MSG and also counts the failure, so the sanity stage can gate the transmit
#define STXG2_CHECK(failures, cond, msg)                                                                                                   \
    do {                                                                                                                                   \
        bool stxg2_check_ok = (cond);                                                                                                      \
        PVDX_ASSERT_MSG(stxg2_check_ok, msg);                                                                                              \
        if (!stxg2_check_ok) {                                                                                                             \
            (failures)++;                                                                                                                  \
        }                                                                                                                                  \
    } while (0)

#if defined(UNITTEST)
// Reads one register and logs it. Leaves *value at 0 if the read fails.
static status_t test_stxg2_read_log(const char *name, uint16_t addr, uint32_t *value) {
    *value = 0;
    status_t status = stxg2_read_reg(addr, value);
    test_log("%-12s 0x%04x = 0x%08lx (status %d)\n", name, addr, (unsigned long)*value, status);
    return status;
}

// Powers on the radio and checks its identity and health registers. Returns the number of failed checks.
static int test_stxg2_sanity(void) {
    int failures = 0;
    uint32_t v = 0;

    STXG2_CHECK(failures, stxg2_init() == SUCCESS, "stxg2 init\n");
    if (failures != 0) {
        return failures;
    }

    STXG2_CHECK(failures, test_stxg2_read_log("ID", REG_ID, &v) == SUCCESS && v == STXG2_ID_VALUE, "stxg2 id\n");
    STXG2_CHECK(failures, test_stxg2_read_log("SERIAL", REG_SERIAL, &v) == SUCCESS, "stxg2 serial\n");
    STXG2_CHECK(failures, test_stxg2_read_log("SW_VERSION", REG_SW_VERSION, &v) == SUCCESS, "stxg2 sw version\n");
    STXG2_CHECK(failures, test_stxg2_read_log("FW_VERSION", REG_FW_VERSION, &v) == SUCCESS, "stxg2 fw version\n");
    STXG2_CHECK(failures, test_stxg2_read_log("HW_VERSION", REG_HW_VERSION, &v) == SUCCESS, "stxg2 hw version\n");

    // The uptime must advance, which shows that the radio is running and not just answering the bus
    uint32_t uptime_start = 0;
    uint32_t uptime_end = 0;
    STXG2_CHECK(failures, test_stxg2_read_log("UPTIME", REG_UPTIME, &uptime_start) == SUCCESS, "stxg2 uptime read\n");
    delay_ms(50);
    STXG2_CHECK(failures, test_stxg2_read_log("UPTIME", REG_UPTIME, &uptime_end) == SUCCESS && uptime_end > uptime_start,
                "stxg2 uptime advances\n");

    // I2C_STAT clears on read and has no flags when the bus is healthy
    STXG2_CHECK(failures, test_stxg2_read_log("I2C_STATUS", REG_I2C_STATUS, &v) == SUCCESS && (v & I2C_STATUS_MSK) == 0,
                "stxg2 i2c status\n");

    // Read BOARD_STAT first: the current and voltage flags clear when the error registers are read
    STXG2_CHECK(failures, test_stxg2_read_log("BOARD_STATUS", REG_BOARD_STATUS, &v) == SUCCESS && (v & BOARD_STATUS_FAULT_MSK) == 0,
                "stxg2 board status\n");
    STXG2_CHECK(failures, test_stxg2_read_log("CURRENT_ERR", REG_CURRENT_ERR, &v) == SUCCESS && v == 0, "stxg2 current errors\n");
    STXG2_CHECK(failures, test_stxg2_read_log("VOLTAGE_ERR", REG_VOLTAGE_ERR, &v) == SUCCESS && v == 0, "stxg2 voltage errors\n");

    for (uint16_t i = 0; i < 4; i++) {
        STXG2_CHECK(failures, test_stxg2_read_log("TEMP", REG_TEMP_0 + i, &v) == SUCCESS, "stxg2 temperature read\n");
    }
    return failures;
}

// Puts the radio in Config mode and sets the frequency, symbol rate, MODCOD and power for either pattern
static status_t test_stxg2_configure_radio(void) {
    ret_err_status(stxg2_set_mode(STXG2_MODE_CONFIG), "stxg2 test: config mode failed\n");
    ret_err_status(stxg2_configure_rf(STXG2_TEST_FREQ_KHZ, STXG2_TEST_SYM_RATE_KSPS, STXG2_TEST_MODCOD, STXG2_POWER_MIN_DBM),
                   "stxg2 test: RF configuration failed\n");

    // The default 18 minute PA timeout would drop the radio back to Standby
    ret_err_status(stxg2_write_reg(REG_PA_TIMEOUT, PA_TIMEOUT_NO_LIMIT << 16), "stxg2 test: PA timeout write failed\n");
    return SUCCESS;
}

    #if STXG2_TEST_PATTERN == STXG2_TEST_PATTERN_TPGEN
// Starts transmitting the radio's own test pattern. Returns SUCCESS once the radio is in Transmit mode.
static status_t test_stxg2_start_tpgen(void) {
    // The test pattern goes to the encoder input, with the radio framer off, as in stxg2_downlink_begin
    ret_err_status(stxg2_route(XBAR_IN_TPGEN, XBAR_OUT_FRAMER_SHIFT), "stxg2 test: route test pattern failed\n");

    // Lock RF before the PA turns on
    uint32_t rf_status = 0;
    for (uint32_t waited_ms = 0; (rf_status & RF_STATUS_LOCKED) != RF_STATUS_LOCKED; waited_ms += 10) {
        if (waited_ms >= STXG2_TEST_LOCK_TIMEOUT_MS) {
            warning("stxg2 test: RF synthesizers not locked (RF_STAT = 0x%lx)\n", (unsigned long)rf_status);
            return ERROR_NOT_READY;
        }
        watchdog_feed(WDT);
        delay_ms(10);
        ret_err_status(stxg2_get_rf_status(&rf_status), "stxg2 test: RF status read failed\n");
    }

    ret_err_status(stxg2_set_mode(STXG2_MODE_TRANSMIT), "stxg2 test: transmit mode failed\n");

    // Entering Transmit can turn the framer back on, so turn it off again before the pattern starts
    uint32_t tf_ctrl = 0;
    ret_err_status(stxg2_read_reg(REG_TF_CTRL, &tf_ctrl), "stxg2 test: TF_CTRL read failed\n");
    ret_err_status(stxg2_write_reg(REG_TF_CTRL, tf_ctrl & ~TF_CTRL_ENABLE), "stxg2 test: framer disable failed\n");

    ret_err_status(stxg2_write_reg(REG_TP_GEN_CTRL, TP_GEN_CTRL_ENABLE), "stxg2 test: test pattern start failed\n");
    return SUCCESS;
}
    #endif // STXG2_TEST_PATTERN_TPGEN

    #if STXG2_TEST_PATTERN == STXG2_TEST_PATTERN_CALLSIGN
        #define STXG2_TEST_ID_TEXT "DE " STXG2_TEST_CALLSIGN " PVDX TEST "
        #define STXG2_TEST_ID_BYTES (sizeof(STXG2_TEST_ID_TEXT) - 1U)
        #define STXG2_TEST_BLOCK_BYTES (STXG2_TEST_ID_BYTES + STXG2_TEST_PRBS_BYTES)

// Fills out with PRBS7 (x^7 + x^6 + 1, seed 0x02, the radio generator's polynomial), most significant bit first
static void test_stxg2_fill_prbs7(uint8_t *out, size_t len) {
    uint8_t lfsr = 0x02;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = 0;
        for (int bit = 0; bit < 8; bit++) {
            uint8_t next = ((lfsr >> 6) ^ (lfsr >> 5)) & 1u;
            lfsr = (uint8_t)(((lfsr << 1) | next) & 0x7Fu);
            byte = (uint8_t)((byte << 1) | next);
        }
        out[i] = byte;
    }
}

// Stores one full memory packet (STXG2_PACKET_DATA_BYTES) of the repeating ID text and PRBS7 block
static status_t test_stxg2_store_callsign_packet(void) {
    uint8_t block[STXG2_TEST_BLOCK_BYTES];
    memcpy(block, STXG2_TEST_ID_TEXT, STXG2_TEST_ID_BYTES);
    test_stxg2_fill_prbs7(&block[STXG2_TEST_ID_BYTES], STXG2_TEST_PRBS_BYTES);

    ret_err_status(stxg2_store_begin(STXG2_TEST_MEM_INDEX), "stxg2 test: store begin failed\n");
    status_t status = SUCCESS;
    for (size_t remaining = STXG2_PACKET_DATA_BYTES; remaining > 0 && status == SUCCESS;) {
        size_t n = remaining < sizeof(block) ? remaining : sizeof(block);
        watchdog_feed(WDT);
        status = stxg2_store(block, n);
        remaining -= n;
    }

    // Always end the session so SPI and the crossbar are released
    uint32_t packets = 0;
    status_t end_status = stxg2_store_end(&packets);
    test_log("stxg2: stored %lu packet(s) (store %d, end %d)\n", (unsigned long)packets, status, end_status);
    if (status != SUCCESS) {
        return status;
    }
    if (end_status != SUCCESS) {
        return end_status;
    }
    return packets == 1 ? SUCCESS : ERROR_SANITY_CHECK_FAILED;
}

// Waits until the radio has sent the whole stored packet. Returns ERROR_NOT_READY on timeout.
static status_t test_stxg2_wait_packet_sent(void) {
    for (uint32_t waited_ms = 0; waited_ms < STXG2_TEST_PACKET_TIMEOUT_MS; waited_ms += 10) {
        uint32_t rd_stat = 0;
        ret_err_status(stxg2_read_reg(REG_MEM_RD_STAT, &rd_stat), "stxg2 test: memory read status failed\n");
        if (rd_stat & (MEM_STAT_ERROR | MEM_STAT_OOB | MEM_STAT_INVALID_INDEX)) {
            warning("stxg2 test: memory read error (MEM_RD_STAT = 0x%lx)\n", (unsigned long)rd_stat);
            return ERROR_READ_FAILED;
        }
        if (rd_stat & MEM_STAT_COMPLETE) {
            return SUCCESS;
        }
        watchdog_feed(WDT);
        delay_ms(10);
    }
    return ERROR_NOT_READY;
}
    #endif // STXG2_TEST_PATTERN_CALLSIGN

static void test_stxg2_report(uint32_t counter) {
    uint32_t mode = 0;
    uint32_t rf_status = 0;
    uint32_t board_status = 0;
    uint32_t temp = 0;
    status_t status = stxg2_read_reg(REG_MODE, &mode);
    status |= stxg2_get_rf_status(&rf_status);
    status |= stxg2_get_board_status(&board_status);
    status |= stxg2_read_reg(REG_TEMP_0, &temp);
    test_log("tx: mode 0x%lx  rf 0x%lx  board 0x%lx  count %lu  pa temp 0x%lx  (status %d)\n", (unsigned long)mode,
             (unsigned long)rf_status, (unsigned long)board_status, (unsigned long)counter, (unsigned long)temp, status);

    if (status == SUCCESS) {
        PVDX_ASSERT_MSG((mode & MODE_MODE_MSK) == STXG2_MODE_TRANSMIT, "stxg2 still in transmit mode\n");
        PVDX_ASSERT_MSG((rf_status & RF_STATUS_LOCKED) == RF_STATUS_LOCKED, "stxg2 rf still locked\n");
    }
}

void test_stxg2(void) {
    test_log("----- testing stxg2 hw -----\n");

    if (test_stxg2_sanity() != 0) {
        test_log("stxg2: sanity checks failed, skipping transmit\n");
        return;
    }

    #if STXG2_TEST_PATTERN == STXG2_TEST_PATTERN_CALLSIGN
    // The packet goes into flash first, because storing needs Flash Access mode
    status_t status = test_stxg2_store_callsign_packet();
    PVDX_ASSERT_MSG(status == SUCCESS, "stxg2 store callsign packet\n");
    if (status == SUCCESS) {
        status = test_stxg2_configure_radio();
    }
    if (status == SUCCESS) {
        status = stxg2_downlink_begin(STXG2_TEST_MEM_INDEX, 1);
    }
    #else
    status_t status = test_stxg2_configure_radio();
    if (status == SUCCESS) {
        status = test_stxg2_start_tpgen();
    }
    #endif
    PVDX_ASSERT_MSG(status == SUCCESS, "stxg2 start transmit\n");
    if (status != SUCCESS) {
        test_log("stxg2: could not start transmit, leaving Standby\n");
        (void)stxg2_set_mode(STXG2_MODE_STANDBY);
        return;
    }

    // Transmit until the board is reset. The watchdog task is not running yet, so feed it here.
    test_log("stxg2: transmitting at %u dBm, MODCOD %u, %s\n", (unsigned)STXG2_POWER_MIN_DBM, (unsigned)STXG2_TEST_MODCOD,
             STXG2_TEST_PATTERN == STXG2_TEST_PATTERN_CALLSIGN ? "callsign pattern" : "test pattern");
    #if STXG2_TEST_PATTERN == STXG2_TEST_PATTERN_CALLSIGN
    // The radio sends one packet per downlink. The carrier drops for a moment between packets while the radio re-enters Config.
    uint32_t packets_sent = 0;
    while (true) {
        status = test_stxg2_wait_packet_sent();
        if (status == SUCCESS) {
            packets_sent++;
        }
        PVDX_ASSERT_MSG(status == SUCCESS, "stxg2 packet sent\n");
        test_stxg2_report(packets_sent);

        status = stxg2_downlink_end();
        if (status == SUCCESS) {
            status = stxg2_downlink_begin(STXG2_TEST_MEM_INDEX, 1);
        }
        PVDX_ASSERT_MSG(status == SUCCESS, "stxg2 restart packet\n");
        if (status != SUCCESS) {
            test_log("stxg2: could not restart transmit, leaving Standby\n");
            (void)stxg2_set_mode(STXG2_MODE_STANDBY);
            return;
        }
    }
    #else
    while (true) {
        watchdog_feed(WDT);
        delay_ms(STXG2_TEST_REPORT_PERIOD_MS);
        uint32_t tp_count = 0;
        (void)stxg2_read_reg(REG_TP_GEN_CNT, &tp_count);
        test_stxg2_report(tp_count);
    }
    #endif
}
#endif // UNITTEST
