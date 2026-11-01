/**
 * test_stxg2.c
 *
 * On-board test of the STXG2 S-band transmitter. Powers the radio on and reads its identity and health registers. If they are
 * good, it transmits DVB-S2 at 2418 MHz, 100 ksps, MODCOD 4 (QPSK 1/2, about 97 kbps of data) and the lowest power.
 * Each setting is written in Config mode and read back before the PA turns on. STXG2_TEST_PATTERN selects the data:
 *  - STXG2_TEST_PATTERN_TPGEN: 8-bit counter from the STXG2's internal test pattern generator. The radio's transfer framer
 *    puts it in CCSDS TM frames (ASM + CRC-16). The ground decoder (GNURadio/sband in radio-testing) checks the frames and
 *    the counter with `decode_dvbs2.py --pattern counter`. Should not be transmitted over a full link.
 *  - STXG2_TEST_PATTERN_CALLSIGN: Repeats "DE <callsign> PVDX TEST " followed by a PRBS7 sequence. The packet is
 *    stored in the radio's flash over SPI and then sent again and again.
 * Requires the STXG2 hardware with an antenna or a 50 ohm load on the RF port. Results can be obtained over RTT.
 *
 * test_stxg2_sweep transmits the test pattern at each frequency from STXG2_SWEEP_START_KHZ to STXG2_SWEEP_STOP_KHZ for
 * STXG2_SWEEP_DWELL_MS, then goes to Standby. Use it with a spectrum analyzer in max hold to measure an antenna. The
 * signal has no callsign ID, so radiate it only in a shielded room or an anechoic chamber.
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

#ifndef STXG2_TEST_FREQ_KHZ
    #define STXG2_TEST_FREQ_KHZ 2418000U
#endif
#if STXG2_TEST_FREQ_KHZ < STXG2_FREQ_MIN_KHZ || STXG2_TEST_FREQ_KHZ > STXG2_FREQ_MAX_KHZ || STXG2_TEST_FREQ_KHZ % STXG2_FREQ_STEP_KHZ != 0
    #error "STXG2_TEST_FREQ_KHZ must be in STXG2_FREQ_MIN_KHZ..STXG2_FREQ_MAX_KHZ, in STXG2_FREQ_STEP_KHZ steps"
#endif
#ifndef STXG2_TEST_SYM_RATE_KSPS
    #define STXG2_TEST_SYM_RATE_KSPS 100U // USM 9.2.5.2: 100 ksps is the lowest rate
#endif
#if STXG2_TEST_SYM_RATE_KSPS < 100U || STXG2_TEST_SYM_RATE_KSPS > STXG2_SYM_RATE_MAX_KSPS || STXG2_TEST_SYM_RATE_KSPS % 100U != 0
    #error "STXG2_TEST_SYM_RATE_KSPS must be 100..STXG2_SYM_RATE_MAX_KSPS, in 100 ksps steps"
#endif
#ifndef STXG2_TEST_MODCOD
    #define STXG2_TEST_MODCOD 4U // QPSK 1/2 (USM table 9-14). About 97 kbps of data at 100 ksps with pilots.
#endif
#ifndef STXG2_TEST_POWER_DBM
    #define STXG2_TEST_POWER_DBM STXG2_POWER_MIN_DBM
#endif
#if STXG2_TEST_MODCOD < 1 || STXG2_TEST_MODCOD > DVBS2_MODCOD_MAX
    #error "STXG2_TEST_MODCOD must be 1..DVBS2_MODCOD_MAX"
#endif
#if STXG2_TEST_POWER_DBM < STXG2_POWER_MIN_DBM || STXG2_TEST_POWER_DBM > STXG2_POWER_MAX_DBM
    #error "STXG2_TEST_POWER_DBM must be STXG2_POWER_MIN_DBM..STXG2_POWER_MAX_DBM"
#endif
#ifndef STXG2_TEST_TX_SECONDS
    #define STXG2_TEST_TX_SECONDS 30U // Transmit time. Then the radio goes to Standby and is powered off.
#endif
// The radio PA timeout (PA_TIMEOUT, seconds) stops the PA by itself this long after the MCU should have
#define STXG2_TEST_PA_BACKSTOP_SECONDS 5U
#if STXG2_TEST_TX_SECONDS == 0 || STXG2_TEST_TX_SECONDS + STXG2_TEST_PA_BACKSTOP_SECONDS > 0xFFFFU
    #error "STXG2_TEST_TX_SECONDS must be 1..65530"
#endif
#define STXG2_TEST_LOCK_TIMEOUT_MS 5000U
#define STXG2_TEST_PA_OFF_TIMEOUT_MS 2000U
#define STXG2_TEST_OFF_SETTLE_MS 1000U // Standby to power off. Estimate: the USM gives no figure.
#define STXG2_TEST_REPORT_PERIOD_MS 1000U
#define STXG2_TEST_REPORT_EVERY 10U
#define STXG2_TEST_MAX_PROBLEM_LOGS 5U
#define STXG2_TEST_MEM_INDEX 0U             // Flash packet that holds the callsign pattern
#define STXG2_TEST_PRBS_BYTES 64U           // PRBS7 bytes after each ID text
#define STXG2_TEST_PACKET_TIMEOUT_MS 10000U // Generous: one packet takes well under a second at 1 Msps

// Override these and STXG2_TEST_MODCOD/STXG2_TEST_POWER_DBM with `make test TESTS=stxg2_sweep <SETTING>=<value>`
#ifndef STXG2_SWEEP_START_KHZ
    #define STXG2_SWEEP_START_KHZ 2412000U
#endif
#ifndef STXG2_SWEEP_STOP_KHZ
    #define STXG2_SWEEP_STOP_KHZ 2430000U
#endif
#ifndef STXG2_SWEEP_STEP_KHZ
    #define STXG2_SWEEP_STEP_KHZ 1000U
#endif
#ifndef STXG2_SWEEP_DWELL_MS
    #define STXG2_SWEEP_DWELL_MS 10000U
#endif
#if STXG2_SWEEP_START_KHZ < STXG2_FREQ_MIN_KHZ || STXG2_SWEEP_STOP_KHZ > STXG2_FREQ_MAX_KHZ || STXG2_SWEEP_START_KHZ > STXG2_SWEEP_STOP_KHZ
    #error "The sweep must stay inside STXG2_FREQ_MIN_KHZ..STXG2_FREQ_MAX_KHZ"
#endif
#if STXG2_SWEEP_STEP_KHZ == 0 || STXG2_SWEEP_STEP_KHZ % STXG2_FREQ_STEP_KHZ != 0
    #error "STXG2_SWEEP_STEP_KHZ must be a nonzero multiple of STXG2_FREQ_STEP_KHZ"
#endif
#if STXG2_SWEEP_DWELL_MS == 0
    #error "STXG2_SWEEP_DWELL_MS must be more than 0"
#endif

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

// PA_TIMEOUT (USM 9.2.9.4). Bits 31:16 are the PA on-time limit in seconds. At the limit, the radio goes to Standby.
#define PA_TIMEOUT_LIMIT_POS 16u
#define PA_TIMEOUT_LIMIT_MSK (0xFFFFu << PA_TIMEOUT_LIMIT_POS)

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

// Kbch in bits for each MODCOD, normal and short FECFRAMEs (ETSI EN 302 307-1 tables 5a, 5b). 0 = not defined.
static const uint16_t stxg2_kbch_normal[DVBS2_MODCOD_MAX + 1] = {
    0,     16008, 21408, 25728, 32208, 38688, 43040, 48408, 51648, 53840, 57472, 58192, 38688, 43040, 48408,
    53840, 57472, 58192, 43040, 48408, 51648, 53840, 57472, 58192, 48408, 51648, 53840, 57472, 58192};
static const uint16_t stxg2_kbch_short[DVBS2_MODCOD_MAX + 1] = {
    0,     3072,  5232, 6312,  7032, 9552, 10632, 11712, 12432, 13152, 14232, 0,     9552,  10632, 11712,
    13152, 14232, 0,    10632, 11712, 12432, 13152, 14232, 0,     11712, 12432, 13152, 14232, 0};

// Bits per symbol for each MODCOD: QPSK 1-11, 8PSK 12-17, 16APSK 18-23, 32APSK 24-28
static uint32_t stxg2_bits_per_symbol(uint32_t modcod) {
    return modcod <= 11 ? 2u : modcod <= 17 ? 3u : modcod <= 23 ? 4u : 5u;
}

// Returns the BBFRAME data rate in bit/s: (Kbch - 80 header bits) per PLFRAME. 0 if the MODCOD is not valid.
static uint32_t stxg2_expected_data_bps(uint32_t sym_rate_ksps, uint32_t dvbs2_conf) {
    uint32_t modcod = dvbs2_conf & DVBS2_CONF_MODCOD_MSK;
    bool short_frame = (dvbs2_conf & DVBS2_CONF_SHORT_FRAME) != 0;
    if (modcod == 0 || modcod > DVBS2_MODCOD_MAX) {
        return 0;
    }
    uint32_t kbch = short_frame ? stxg2_kbch_short[modcod] : stxg2_kbch_normal[modcod];
    if (kbch == 0) {
        return 0;
    }
    uint32_t slots = (short_frame ? 16200u : 64800u) / stxg2_bits_per_symbol(modcod) / 90u;
    uint32_t pilot_blocks = (dvbs2_conf & DVBS2_CONF_PILOTS) ? (slots - 1u) / 16u : 0u;
    uint32_t plframe_symbols = 90u + slots * 90u + pilot_blocks * 36u;
    return (uint32_t)((uint64_t)sym_rate_ksps * 1000u * (kbch - 80u) / plframe_symbols);
}

// Writes a register, reads it back and checks the bits in mask
static status_t test_stxg2_write_verify(const char *name, uint16_t addr, uint32_t value, uint32_t mask) {
    ret_err_status(stxg2_write_reg(addr, value), "stxg2 test: register write failed\n");
    uint32_t readback = 0;
    ret_err_status(test_stxg2_read_log(name, addr, &readback), "stxg2 test: register read failed\n");
    if ((readback & mask) != (value & mask)) {
        warning("stxg2 test: %s kept 0x%08lx, wrote 0x%08lx\n", name, (unsigned long)readback, (unsigned long)value);
        return ERROR_WRITE_FAILED;
    }
    return SUCCESS;
}

// Puts the radio in Config mode and sets DVB-S2 encoding, frequency, symbol rate, MODCOD and power. Every value is read
// back, so a setting that the radio rejects stops the test before the PA turns on.
static status_t test_stxg2_configure_radio(uint32_t freq_khz) {
    ret_err_status(stxg2_set_mode(STXG2_MODE_CONFIG), "stxg2 test: config mode failed\n");

    // ENCODING_CTRL can only change outside Transmit. Bit 0 = 0 selects DVB-S2. Insert_ASM puts the ASM in front of each
    // transfer frame. Keep the factory rolloff, IQ and frame length settings.
    uint32_t enc = 0;
    ret_err_status(test_stxg2_read_log("ENCODING", REG_ENCODING_CTRL, &enc), "stxg2 test: encoding read failed\n");
    enc = (enc & ~ENCODING_CTRL_CCSDS) | ENCODING_CTRL_INSERT_ASM;
    ret_err_status(test_stxg2_write_verify("ENCODING", REG_ENCODING_CTRL, enc, ENCODING_CTRL_CCSDS | ENCODING_CTRL_INSERT_ASM),
                   "stxg2 test: DVB-S2 select failed\n");

    // stxg2_configure_rf rejects rates below STXG2_SYM_RATE_MIN_KSPS, so set the symbol rate after it
    uint32_t sym_rate_arg = STXG2_TEST_SYM_RATE_KSPS < STXG2_SYM_RATE_MIN_KSPS ? STXG2_SYM_RATE_MIN_KSPS : STXG2_TEST_SYM_RATE_KSPS;
    ret_err_status(stxg2_configure_rf(freq_khz, sym_rate_arg, STXG2_TEST_MODCOD, STXG2_TEST_POWER_DBM),
                   "stxg2 test: RF configuration failed\n");
    ret_err_status(test_stxg2_write_verify("SYMBOL_RATE", REG_SYMBOL_RATE, STXG2_TEST_SYM_RATE_KSPS, 0xFFFFFFFFu),
                   "stxg2 test: symbol rate not accepted\n");

    uint32_t v = 0;
    ret_err_status(test_stxg2_read_log("FREQUENCY", REG_FREQUENCY, &v), "stxg2 test: frequency read failed\n");
    if (v != freq_khz) {
        warning("stxg2 test: FREQUENCY kept %lu kHz, wrote %lu kHz\n", (unsigned long)v, (unsigned long)freq_khz);
        return ERROR_WRITE_FAILED;
    }
    uint32_t dvbs2 = 0;
    ret_err_status(test_stxg2_read_log("DVBS2_CONF", REG_DVBS2_CONF, &dvbs2), "stxg2 test: DVB-S2 config read failed\n");
    if ((dvbs2 & DVBS2_CONF_MODCOD_MSK) != STXG2_TEST_MODCOD) {
        warning("stxg2 test: MODCOD kept %lu, wrote %u\n", (unsigned long)(dvbs2 & DVBS2_CONF_MODCOD_MSK), (unsigned)STXG2_TEST_MODCOD);
        return ERROR_WRITE_FAILED;
    }
    ret_err_status(test_stxg2_read_log("PA_CONF_0", REG_PA_CONF_0, &v), "stxg2 test: PA config read failed\n");

    // The ground receiver needs these values
    static const char *const rolloff[] = {"0.35", "0.25", "0.2", "reserved"};
    test_log("stxg2: DVB-S2, %lu kHz, %lu ksps, MODCOD %lu, pilots %s, %s frames, rolloff %s, IQ inv/swap 0x%lx\n",
             (unsigned long)freq_khz, (unsigned long)STXG2_TEST_SYM_RATE_KSPS, (unsigned long)(dvbs2 & DVBS2_CONF_MODCOD_MSK),
             (dvbs2 & DVBS2_CONF_PILOTS) ? "on" : "off", (dvbs2 & DVBS2_CONF_SHORT_FRAME) ? "short" : "normal",
             rolloff[(enc & ENCODING_CTRL_RRC_MSK) >> ENCODING_CTRL_RRC_POS], (unsigned long)((enc & ENCODING_CTRL_IQ_MSK) >> 4));
    test_log("stxg2: expected data rate %lu bit/s\n", (unsigned long)stxg2_expected_data_bps(STXG2_TEST_SYM_RATE_KSPS, dvbs2));
    uint32_t tf_conf = 0;
    (void)stxg2_read_reg(REG_TF_CONF, &tf_conf);
    test_log("stxg2: transfer frames: %lu data bytes, SCID %lu, VCID %lu (idle frames on VCID 7)\n",
             (unsigned long)(enc >> ENCODING_CTRL_FRAME_LEN_POS), (unsigned long)(tf_conf & 0x3FFu), (unsigned long)((tf_conf >> 10) & 0x7u));
    return SUCCESS;
}

// Sets the radio PA timeout to the transmit time plus a margin, so the PA stops even if the MCU does not stop it
static status_t test_stxg2_set_pa_timeout(void) {
    uint32_t limit_s = STXG2_TEST_TX_SECONDS + STXG2_TEST_PA_BACKSTOP_SECONDS;
    return test_stxg2_write_verify("PA_TIMEOUT", REG_PA_TIMEOUT, limit_s << PA_TIMEOUT_LIMIT_POS, PA_TIMEOUT_LIMIT_MSK);
}

// Ends a transmission and turns the radio fully off: Standby, wait for the PA to switch off, settle, then remove power
// with SBAND_EN and SBAND_RST. If a step fails, the radio is powered off anyway.
static void test_stxg2_stop_transmit(void) {
    (void)stxg2_write_reg(REG_TP_GEN_CTRL, 0);

    status_t status = stxg2_set_mode(STXG2_MODE_STANDBY);
    PVDX_ASSERT_MSG(status == SUCCESS, "stxg2 standby after transmit\n");

    uint32_t pa_status = PA_STATUS_PA_ON;
    for (uint32_t waited_ms = 0; waited_ms < STXG2_TEST_PA_OFF_TIMEOUT_MS && (pa_status & PA_STATUS_PA_ON); waited_ms += 10) {
        if (stxg2_read_reg(REG_PA_STATUS_0, &pa_status) != SUCCESS) {
            pa_status = PA_STATUS_PA_ON;
        }
        watchdog_feed(WDT);
        delay_ms(10);
    }
    bool pa_off = (pa_status & PA_STATUS_PA_ON) == 0;
    PVDX_ASSERT_MSG(pa_off, "stxg2 PA off before power off\n");
    if (status != SUCCESS || !pa_off) {
        warning("stxg2 test: radio did not reach Standby with the PA off (PA_STAT_0 = 0x%08lx), powering off anyway\n",
                (unsigned long)pa_status);
    }

    for (uint32_t waited_ms = 0; waited_ms < STXG2_TEST_OFF_SETTLE_MS; waited_ms += 100) {
        watchdog_feed(WDT);
        delay_ms(100);
    }
    stxg2_power_off();
}

// Logs the registers that can explain why the RF synthesizers did not lock
static void test_stxg2_log_lock_failure(void) {
    uint32_t v = 0;
    (void)test_stxg2_read_log("UPTIME", REG_UPTIME, &v);
    (void)test_stxg2_read_log("MODE", REG_MODE, &v);
    (void)test_stxg2_read_log("RF_STATUS", REG_RF_STATUS, &v);
    (void)test_stxg2_read_log("BOARD_STATUS", REG_BOARD_STATUS, &v);
    (void)test_stxg2_read_log("CURRENT_ERR", REG_CURRENT_ERR, &v);
    (void)test_stxg2_read_log("VOLTAGE_ERR", REG_VOLTAGE_ERR, &v);
    for (uint16_t i = 0; i < 4; i++) {
        (void)test_stxg2_read_log("TEMP", REG_TEMP_0 + i, &v);
    }
}

// Starts transmitting the radio's own test pattern. Returns SUCCESS once the radio is in Transmit mode.
static status_t test_stxg2_start_tpgen(void) {
    // The test pattern goes into the transfer framer, which wraps it in CCSDS TM frames for the encoder
    ret_err_status(stxg2_route(XBAR_IN_TPGEN, XBAR_OUT_FRAMER_SHIFT), "stxg2 test: route test pattern failed\n");

    // Lock RF before the PA turns on
    uint32_t rf_status = 0;
    for (uint32_t waited_ms = 0; (rf_status & RF_STATUS_LOCKED) != RF_STATUS_LOCKED; waited_ms += 10) {
        if (waited_ms >= STXG2_TEST_LOCK_TIMEOUT_MS) {
            warning("stxg2 test: RF synthesizers not locked after %u ms (RF_STAT = 0x%lx)\n", STXG2_TEST_LOCK_TIMEOUT_MS,
                    (unsigned long)rf_status);
            test_stxg2_log_lock_failure();
            return ERROR_NOT_READY;
        }
        watchdog_feed(WDT);
        delay_ms(10);
        ret_err_status(stxg2_get_rf_status(&rf_status), "stxg2 test: RF status read failed\n");
    }

    ret_err_status(stxg2_set_mode(STXG2_MODE_TRANSMIT), "stxg2 test: transmit mode failed\n");

    // The radio turns the framer on in Config mode, but stxg2_downlink_end() turns it off. Make sure that it is on, with
    // CRC-16, before the pattern starts.
    uint32_t tf_ctrl = 0;
    ret_err_status(stxg2_read_reg(REG_TF_CTRL, &tf_ctrl), "stxg2 test: TF_CTRL read failed\n");
    ret_err_status(test_stxg2_write_verify("TF_CTRL", REG_TF_CTRL, tf_ctrl | TF_CTRL_ENABLE | TF_CTRL_EN_CRC,
                                           TF_CTRL_ENABLE | TF_CTRL_EN_CRC),
                   "stxg2 test: framer enable failed\n");

    ret_err_status(stxg2_write_reg(REG_TP_GEN_CTRL, TP_GEN_CTRL_ENABLE), "stxg2 test: test pattern start failed\n");
    return SUCCESS;
}

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

// Waits until the radio has sent the whole stored packet. Adds the time waited to *elapsed_ms. Returns ERROR_NOT_READY
// on timeout.
static status_t test_stxg2_wait_packet_sent(uint32_t *elapsed_ms) {
    for (uint32_t waited_ms = 0; waited_ms < STXG2_TEST_PACKET_TIMEOUT_MS; waited_ms += 10, *elapsed_ms += 10) {
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

// Logs the radio state when always_log is set or when something is wrong, and only for the first few problems so a dead radio
// cannot fill the log buffer.
static void test_stxg2_report(uint32_t counter, bool always_log) {
    static bool left_transmit_logged = false;
    static uint32_t problems_logged = 0;
    uint32_t mode = 0;
    uint32_t rf_status = 0;
    uint32_t board_status = 0;
    uint32_t temp = 0;
    uint32_t currents = 0;
    uint32_t pa_voltages = 0;
    uint32_t board_voltages = 0;
    status_t status = stxg2_read_reg(REG_MODE, &mode);
    status |= stxg2_get_rf_status(&rf_status);
    status |= stxg2_get_board_status(&board_status);
    status |= stxg2_read_reg(REG_TEMP_0, &temp);
    status |= stxg2_read_reg(REG_CURRENTS_0, &currents);
    status |= stxg2_read_reg(REG_VOLTAGES_0, &pa_voltages);
    status |= stxg2_read_reg(REG_VOLTAGES_1, &board_voltages);

    bool transmitting = status == SUCCESS && (mode & MODE_MODE_MSK) == STXG2_MODE_TRANSMIT &&
                        (rf_status & RF_STATUS_LOCKED) == RF_STATUS_LOCKED;
    if (!always_log) {
        if (transmitting || problems_logged >= STXG2_TEST_MAX_PROBLEM_LOGS) {
            return;
        }
        problems_logged++;
    }

    test_log("tx: mode 0x%lx rf 0x%lx board 0x%lx cnt %lu pa %d C smps %d C vbat %lu mV pa %lu mV %lu mA (status %d)\n",
             (unsigned long)mode, (unsigned long)rf_status, (unsigned long)board_status, (unsigned long)counter,
             (int16_t)(temp & 0xFFFFU) / 4, (int16_t)(temp >> 16) / 4, (unsigned long)(board_voltages & 0xFFFFU),
             (unsigned long)(pa_voltages & 0xFFFFU), (unsigned long)(currents & 0xFFFFU), status);

    if (status == SUCCESS) {
        PVDX_ASSERT_MSG((mode & MODE_MODE_MSK) == STXG2_MODE_TRANSMIT, "stxg2 still in transmit mode\n");
        PVDX_ASSERT_MSG((rf_status & RF_STATUS_LOCKED) == RF_STATUS_LOCKED, "stxg2 rf still locked\n");
        if ((mode & MODE_MODE_MSK) != STXG2_MODE_TRANSMIT && !left_transmit_logged) {
            left_transmit_logged = true;
            test_stxg2_log_lock_failure();
        }
    }
}

// Waits ms milliseconds and feeds the watchdog, which is not running yet
static void test_stxg2_wait_ms(uint32_t ms) {
    while (ms > 0) {
        uint32_t chunk_ms = ms < STXG2_TEST_REPORT_PERIOD_MS ? ms : STXG2_TEST_REPORT_PERIOD_MS;
        watchdog_feed(WDT);
        delay_ms((uint16_t)chunk_ms);
        ms -= chunk_ms;
    }
}

// Tunes to freq_khz and starts the test pattern. If the RF synthesizers do not lock after a retune, goes through
// Standby so that Config mode powers the RF section up again, and tries once more.
static status_t test_stxg2_tune_and_transmit(uint32_t freq_khz) {
    ret_err_status(test_stxg2_configure_radio(freq_khz), "stxg2 sweep: configure failed\n");
    if (test_stxg2_start_tpgen() == SUCCESS) {
        return SUCCESS;
    }

    test_log("sweep: %lu kHz did not start, restarting RF from Standby\n", (unsigned long)freq_khz);
    ret_err_status(stxg2_set_mode(STXG2_MODE_STANDBY), "stxg2 sweep: standby failed\n");
    ret_err_status(test_stxg2_configure_radio(freq_khz), "stxg2 sweep: configure failed\n");
    ret_err_status(test_stxg2_start_tpgen(), "stxg2 sweep: transmit failed after RF restart\n");
    test_log("sweep: %lu kHz started after RF restart\n", (unsigned long)freq_khz);
    return SUCCESS;
}

// Transmits the test pattern at freq_khz for STXG2_SWEEP_DWELL_MS
static status_t test_stxg2_sweep_step(uint32_t freq_khz) {
    ret_err_status(test_stxg2_tune_and_transmit(freq_khz), "stxg2 sweep: step failed\n");
    test_log("sweep: %lu kHz for %lu ms\n", (unsigned long)freq_khz, (unsigned long)STXG2_SWEEP_DWELL_MS);
    test_stxg2_wait_ms(STXG2_SWEEP_DWELL_MS);

    uint32_t tp_count = 0;
    (void)stxg2_read_reg(REG_TP_GEN_CNT, &tp_count);
    test_stxg2_report(tp_count, true);
    return stxg2_write_reg(REG_TP_GEN_CTRL, 0);
}

void test_stxg2_sweep(void) {
    test_log("----- stxg2 frequency sweep -----\n");

    test_log("stxg2 POST: start\n");
    if (test_stxg2_sanity() != 0) {
        test_log("stxg2 POST: FAILED, skipping sweep, radio powered off\n");
        stxg2_power_off();
        return;
    }
    test_log("stxg2 POST: passed\n");

    test_log("sweep: %lu-%lu kHz, %lu kHz steps, %u dBm, MODCOD %u\n", (unsigned long)STXG2_SWEEP_START_KHZ,
             (unsigned long)STXG2_SWEEP_STOP_KHZ, (unsigned long)STXG2_SWEEP_STEP_KHZ, (unsigned)STXG2_TEST_POWER_DBM,
             (unsigned)STXG2_TEST_MODCOD);
    status_t status = SUCCESS;
    for (uint32_t freq_khz = STXG2_SWEEP_START_KHZ; freq_khz <= STXG2_SWEEP_STOP_KHZ && status == SUCCESS;
         freq_khz += STXG2_SWEEP_STEP_KHZ) {
        status = test_stxg2_sweep_step(freq_khz);
        PVDX_ASSERT_MSG(status == SUCCESS, "stxg2 sweep step\n");
    }

    test_stxg2_stop_transmit();
    test_log("sweep: %s, radio powered off\n", status == SUCCESS ? "done" : "stopped early");
}

void test_stxg2(void) {
    test_log("----- testing stxg2 hw -----\n");

    // POST: power, identity, uptime, I2C, board health, currents, voltages and temperatures. No RF until it passes.
    test_log("stxg2 POST: start\n");
    if (test_stxg2_sanity() != 0) {
        test_log("stxg2 POST: FAILED, skipping transmit, radio powered off\n");
        stxg2_power_off();
        return;
    }
    test_log("stxg2 POST: passed\n");

    #if STXG2_TEST_PATTERN == STXG2_TEST_PATTERN_CALLSIGN
    // The packet goes into flash first, because storing needs Flash Access mode
    status_t status = test_stxg2_store_callsign_packet();
    PVDX_ASSERT_MSG(status == SUCCESS, "stxg2 store callsign packet\n");
    if (status == SUCCESS) {
        status = test_stxg2_configure_radio(STXG2_TEST_FREQ_KHZ);
    }
    if (status == SUCCESS) {
        status = test_stxg2_set_pa_timeout();
    }
    if (status == SUCCESS) {
        status = stxg2_downlink_begin(STXG2_TEST_MEM_INDEX, 1);
    }
    #else
    status_t status = test_stxg2_configure_radio(STXG2_TEST_FREQ_KHZ);
    if (status == SUCCESS) {
        status = test_stxg2_set_pa_timeout();
    }
    if (status == SUCCESS) {
        status = test_stxg2_start_tpgen();
    }
    #endif
    PVDX_ASSERT_MSG(status == SUCCESS, "stxg2 start transmit\n");
    if (status != SUCCESS) {
        test_stxg2_stop_transmit();
        test_log("stxg2: could not start transmit, radio powered off\n");
        return;
    }

    // Transmit for STXG2_TEST_TX_SECONDS, then stop. A reset runs this test again. The watchdog task never runs, so feed
    // it here.
    test_log("stxg2: transmitting at %lu kHz, %lu ksps, %u dBm, MODCOD %u, %s for %u s\n", (unsigned long)STXG2_TEST_FREQ_KHZ,
             (unsigned long)STXG2_TEST_SYM_RATE_KSPS, (unsigned)STXG2_TEST_POWER_DBM, (unsigned)STXG2_TEST_MODCOD,
             STXG2_TEST_PATTERN == STXG2_TEST_PATTERN_CALLSIGN ? "callsign pattern" : "test pattern", (unsigned)STXG2_TEST_TX_SECONDS);
    #if STXG2_TEST_PATTERN == STXG2_TEST_PATTERN_CALLSIGN
    // The radio sends one packet per downlink. The carrier drops for a moment between packets while the radio re-enters Config.
    uint32_t packets_sent = 0;
    uint32_t elapsed_ms = 0;
    while (true) {
        status = test_stxg2_wait_packet_sent(&elapsed_ms);
        if (status == SUCCESS) {
            packets_sent++;
        }
        PVDX_ASSERT_MSG(status == SUCCESS, "stxg2 packet sent\n");
        test_stxg2_report(packets_sent, true);

        status = stxg2_downlink_end();
        if (elapsed_ms >= STXG2_TEST_TX_SECONDS * 1000U) {
            break;
        }
        if (status == SUCCESS) {
            status = stxg2_downlink_begin(STXG2_TEST_MEM_INDEX, 1);
        }
        PVDX_ASSERT_MSG(status == SUCCESS, "stxg2 restart packet\n");
        if (status != SUCCESS) {
            test_stxg2_stop_transmit();
            test_log("stxg2: could not restart transmit, radio powered off\n");
            return;
        }
    }
    #else
    for (uint32_t i = 0; i < STXG2_TEST_TX_SECONDS; i++) {
        watchdog_feed(WDT);
        delay_ms(STXG2_TEST_REPORT_PERIOD_MS);
        uint32_t tp_count = 0;
        (void)stxg2_read_reg(REG_TP_GEN_CNT, &tp_count);
        bool periodic = i % STXG2_TEST_REPORT_EVERY == 0;
        test_stxg2_report(tp_count, periodic);
        if (periodic) {
            // TP_GEN_RATE is a 1 s running average (USM 9.2.8.10). The encoder throttles the generator, so after about 10 s it
            // reads the channel data rate.
            uint32_t tp_rate = 0;
            uint32_t data_frames = 0;
            uint32_t idle_frames = 0;
            (void)stxg2_read_reg(REG_TP_GEN_RATE, &tp_rate);
            (void)stxg2_read_reg(REG_TF_DATA_CNT, &data_frames);
            (void)stxg2_read_reg(REG_TF_OID_CNT, &idle_frames);
            test_log("tx: test pattern %lu bytes, %lu bit/s, frames: %lu data, %lu idle\n", (unsigned long)tp_count,
                     (unsigned long)tp_rate, (unsigned long)data_frames, (unsigned long)idle_frames);
        }
    }
    uint32_t tp_count = 0;
    (void)stxg2_read_reg(REG_TP_GEN_CNT, &tp_count);
    test_stxg2_report(tp_count, true);
    #endif
    test_stxg2_stop_transmit();
    test_log("stxg2: transmitted for %u s, radio powered off\n", (unsigned)STXG2_TEST_TX_SECONDS);
}
#endif // UNITTEST
