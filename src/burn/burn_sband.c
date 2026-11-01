/**
 * burn_sband.c
 *
 * Bench test of the STXG2 S-band transmitter, run from main() after the startup device checks. It checks the radio's
 * identity and health, stores one station identification packet in the radio's flash, transmits that packet for
 * SBAND_TX_SECONDS of carrier time, then returns the radio to Standby and powers it off.
 *
 * The packet repeats "DE <callsign> PVDX TEST " followed by a PRBS7 block, so the callsign goes out several times a
 * second. The packet is stored once and reused: a record in MRAM holds the radio's serial number and a checksum of the
 * pattern, and the store is repeated only when one of those changes or SBAND_TX_FORCE_STORE is defined. If the radio
 * then fails to send the stored packet, the transmit stops at once and the MRAM record is cleared, so the next run
 * stores the packet again rather than radiating without identification.
 *
 * This never touches the burn wire. The burn pin is PB29; the S-band pins are PB16-PB19, PC25 and PC26.
 */

#include "burn/burn_sband.h"

#include "globals.h"

#if defined(SBAND_TX_TEST) && defined(DEVBUILD)

    #include <string.h>

    #include "atmel_start.h"
    #include "drivers/stxg2/stxg2.h"
    #include "hal_delay.h"
    #include "logging.h"
    #include "mram_driver.h"
    #include "watchdog_driver.h"

    // Station identification. SBAND_TX_CALLSIGN is deliberately not defaulted: an on-air ID must be the operator's own.
    #ifndef SBAND_TX_CALLSIGN
        #define SBAND_TX_CALLSIGN "AC1VL"
    #endif
    #define SBAND_TX_ID_TEXT "DE " SBAND_TX_CALLSIGN " PVDX TEST "
    #define SBAND_TX_ID_BYTES (sizeof(SBAND_TX_ID_TEXT) - 1u)
    #define SBAND_TX_PRBS_BYTES 64u
    #define SBAND_TX_BLOCK_BYTES (SBAND_TX_ID_BYTES + SBAND_TX_PRBS_BYTES)

    // RF settings. 2418500 kHz is in the STXG2.4 band and on its 100 kHz grid. stxg2_configure_rf() logs the frequency
    // and symbol rate the radio reads back, which on our unit does not always match what it transmits, so confirm both
    // on a spectrum analyzer rather than from the log.
    #define SBAND_TX_FREQ_KHZ 2418500u
    #define SBAND_TX_SYM_RATE_KSPS STXG2_SYM_RATE_DEFAULT_KSPS
    #define SBAND_TX_MODCOD DVBS2_MODCOD_QPSK_1_2
    #define SBAND_TX_POWER_DBM 27u // Flight model range is 27-33 dBm (USM 9.2.9.2)
    #define SBAND_TX_SECONDS 30u   // Carrier time, not counting the checks or the gaps between packets

    #define SBAND_TX_MEM_INDEX 0u            // Flash packet that holds the identification pattern
    #define SBAND_TX_PACKET_TIMEOUT_MS 10000u // One packet takes about 1.3 s at 100 ksps on QPSK 1/2
// 30 s of carrier is about 23 packets. This bound only trips if the radio reports a packet complete without taking the
// time that sending one needs, which would otherwise spin here forever with the carrier time never adding up.
    #define SBAND_TX_MAX_PACKETS 100u
    #define SBAND_TX_POLL_MS 10u
    #define SBAND_TX_REPORT_PERIOD_MS 1000u

    // BOARD_STAT flags (USM 9.2.4.1) that mean a fault. The RF lock flag is checked separately.
    #define BOARD_STATUS_MEM (1u << 3)
    #define BOARD_STATUS_PA (1u << 5)
    #define BOARD_STATUS_PA_WARN (1u << 6)
    #define BOARD_STATUS_TIMEOUT (1u << 7)
    #define BOARD_STATUS_BRD_WARN (1u << 8)
    #define BOARD_STATUS_BRD_TRIP (1u << 9)
    #define BOARD_STATUS_BRD_CURR (1u << 10)
    #define BOARD_STATUS_BRD_VOLT (1u << 11)
    #define BOARD_STATUS_FAULT_MSK                                                                                                         \
        (BOARD_STATUS_MEM | BOARD_STATUS_PA | BOARD_STATUS_PA_WARN | BOARD_STATUS_TIMEOUT | BOARD_STATUS_BRD_WARN |                        \
         BOARD_STATUS_BRD_TRIP | BOARD_STATUS_BRD_CURR | BOARD_STATUS_BRD_VOLT)

/* ---------- STORED PACKET RECORD IN MRAM ---------- */

// Its own 256-byte page, below the burn record at 0x000FFF00 and clear of the flash mirror and the MRAM test region
    #define SBAND_RECORD_MRAM_ADDRESS 0x000FFE00u
    #define SBAND_RECORD_PAGE_SIZE 256u
    #define SBAND_RECORD_MAGIC 0x5BA2D0C5u

// Persists across reboots. The record is only trusted when it names the radio that is on the bus now.
typedef struct {
    uint32_t magic;
    uint32_t radio_serial;  // REG_SERIAL of the radio the packet was stored in
    uint32_t pattern_check; // Checksum of the identification block, so an edited callsign forces a new store
} sband_record_t;

static sband_record_t load_sband_record(void) {
    // On the stack, because the image runs from RAM and .bss is tight. uint32_t so that the page is word-aligned and
    // can be read as an sband_record_t; mram_read_bytes() only reads whole pages.
    uint32_t page[SBAND_RECORD_PAGE_SIZE / sizeof(uint32_t)];
    mram_read_bytes(SBAND_RECORD_MRAM_ADDRESS, (uint8_t *)page, SBAND_RECORD_PAGE_SIZE);

    sband_record_t record = *(const sband_record_t *)page;
    if (record.magic != SBAND_RECORD_MAGIC) {
        record = (sband_record_t){0};
    }
    return record;
}

static void save_sband_record(const sband_record_t *const p_record) {
    mram_write_bytes(SBAND_RECORD_MRAM_ADDRESS, (const uint8_t *)p_record, sizeof(*p_record));
}

static void clear_sband_record(void) {
    const sband_record_t empty = {0};
    save_sband_record(&empty);
}

/* ---------- IDENTIFICATION PATTERN ---------- */

// Fills out with PRBS7 (x^7 + x^6 + 1, seed 0x02, the radio generator's polynomial), most significant bit first
static void fill_prbs7(uint8_t *out, size_t len) {
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

static void build_id_block(uint8_t *block) {
    memcpy(block, SBAND_TX_ID_TEXT, SBAND_TX_ID_BYTES);
    fill_prbs7(&block[SBAND_TX_ID_BYTES], SBAND_TX_PRBS_BYTES);
}

// Fletcher-32 over the block, so that changing the callsign or the pattern invalidates the stored packet
static uint32_t checksum_block(const uint8_t *block, size_t len) {
    uint32_t sum_a = 0xFFFFu;
    uint32_t sum_b = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        sum_a = (sum_a + block[i]) % 0xFFFFu;
        sum_b = (sum_b + sum_a) % 0xFFFFu;
    }
    return (sum_b << 16) | sum_a;
}

/* ---------- HELPERS ---------- */

// Busy-waits and feeds the watchdog. The scheduler is not running, so delay_ms() owns SysTick on its own.
static void wait_ms_feeding_watchdog(uint32_t ms) {
    for (uint32_t waited = 0; waited < ms; waited += SBAND_TX_POLL_MS) {
        watchdog_feed(WDT);
        delay_ms((uint16_t)SBAND_TX_POLL_MS);
    }
}

static status_t read_log(const char *name, uint16_t addr, uint32_t *value) {
    *value = 0;
    status_t status = stxg2_read_reg(addr, value);
    info("sband: %-12s 0x%04x = 0x%08lx (status %d)\n", name, addr, (unsigned long)*value, status);
    return status;
}

// Powers the radio on and checks its identity and health. Returns the number of failed checks and the serial number.
static int sband_sanity(uint32_t *serial) {
    int failures = 0;
    uint32_t v = 0;

    *serial = 0;
    if (stxg2_init() != SUCCESS) {
        warning("sband: radio did not initialize\n");
        return 1;
    }

    // read_log() prints every value, so a failed check only has to be counted here
    failures += read_log("ID", REG_ID, &v) != SUCCESS || v != STXG2_ID_VALUE;
    failures += read_log("SERIAL", REG_SERIAL, serial) != SUCCESS;

    // The uptime must advance, which shows that the radio is running and not just answering the bus
    uint32_t uptime_start = 0;
    uint32_t uptime_end = 0;
    failures += read_log("UPTIME", REG_UPTIME, &uptime_start) != SUCCESS;
    wait_ms_feeding_watchdog(50);
    failures += read_log("UPTIME", REG_UPTIME, &uptime_end) != SUCCESS || uptime_end <= uptime_start;

    // I2C_STAT clears on read and has no flags when the bus is healthy
    failures += read_log("I2C_STAT", REG_I2C_STATUS, &v) != SUCCESS || (v & I2C_STATUS_MSK) != 0;

    // Read BOARD_STAT first: the current and voltage flags clear when the error registers are read
    failures += read_log("BOARD_STAT", REG_BOARD_STATUS, &v) != SUCCESS || (v & BOARD_STATUS_FAULT_MSK) != 0;
    failures += read_log("CURR_ERR", REG_CURRENT_ERR, &v) != SUCCESS || v != 0;
    failures += read_log("VOLT_ERR", REG_VOLTAGE_ERR, &v) != SUCCESS || v != 0;
    for (uint16_t i = 0; i < 4; i++) {
        (void)read_log("TEMP", REG_TEMP_0 + i, &v);
    }

    if (failures != 0) {
        warning("sband: %d check(s) failed, see the register values above\n", failures);
    }
    return failures;
}

// Stores one full memory packet of the repeating identification block
static status_t store_id_packet(const uint8_t *block) {
    status_t status = stxg2_store_begin(SBAND_TX_MEM_INDEX);
    if (status != SUCCESS) {
        return status;
    }
    for (size_t remaining = STXG2_PACKET_DATA_BYTES; remaining > 0 && status == SUCCESS;) {
        size_t n = remaining < SBAND_TX_BLOCK_BYTES ? remaining : SBAND_TX_BLOCK_BYTES;
        watchdog_feed(WDT);
        status = stxg2_store(block, n);
        remaining -= n;
    }

    // Always end the session so SPI and the crossbar are released
    uint32_t packets = 0;
    status_t end_status = stxg2_store_end(&packets);
    info("sband: stored %lu packet(s) (store %d, end %d)\n", (unsigned long)packets, status, end_status);
    if (status != SUCCESS) {
        return status;
    }
    if (end_status != SUCCESS) {
        return end_status;
    }
    return packets == 1 ? SUCCESS : ERROR_SANITY_CHECK_FAILED;
}

// Logs what the radio is doing while the PA is on
static void report_transmit(uint32_t packets_sent) {
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

    info("sband tx: pkts %lu mode 0x%lx rf 0x%lx brd 0x%lx pa %d C %lu mV %lu mA vbat %lu mV (%d)\n",
         (unsigned long)packets_sent, (unsigned long)mode, (unsigned long)rf_status, (unsigned long)board_status,
         (int16_t)(temp & 0xFFFFu) / 4, (unsigned long)(pa_voltages & 0xFFFFu), (unsigned long)(currents & 0xFFFFu),
         (unsigned long)(board_voltages & 0xFFFFu), status);
}

/**
 * \brief Waits for the radio to finish sending the stored packet
 *
 * \param elapsed_ms incremented by the time the PA spent on
 * \returns ERROR_READ_FAILED if the radio reports a memory read error, ERROR_NOT_READY on timeout
 */
static status_t wait_packet_sent(uint32_t *elapsed_ms) {
    for (uint32_t waited = 0; waited < SBAND_TX_PACKET_TIMEOUT_MS; waited += SBAND_TX_POLL_MS) {
        uint32_t rd_stat = 0;
        status_t status = stxg2_read_reg(REG_MEM_RD_STAT, &rd_stat);
        if (status != SUCCESS) {
            return status;
        }
        if (rd_stat & (MEM_STAT_ERROR | MEM_STAT_OOB | MEM_STAT_INVALID_INDEX)) {
            warning("sband: memory read error (MEM_RD_STAT = 0x%lx)\n", (unsigned long)rd_stat);
            return ERROR_READ_FAILED;
        }
        if (rd_stat & MEM_STAT_COMPLETE) {
            return SUCCESS;
        }
        watchdog_feed(WDT);
        delay_ms((uint16_t)SBAND_TX_POLL_MS);
        *elapsed_ms += SBAND_TX_POLL_MS;
    }
    warning("sband: packet not sent after %lu ms\n", (unsigned long)SBAND_TX_PACKET_TIMEOUT_MS);
    return ERROR_NOT_READY;
}

// Commands Standby and cuts power, in that order: the radio must reach Standby before it loses power (USM 8.2.1)
static void shut_radio_down(void) {
    status_t status = stxg2_set_mode(STXG2_MODE_STANDBY);
    if (status != SUCCESS) {
        warning("sband: could not reach Standby before power off (status %d)\n", status);
    }
    stxg2_power_off();
    info("sband: radio in Standby and powered off\n");
}

/* ---------- ENTRY POINT ---------- */

void sband_tx_test(void) {
    info("----- s-band transmit test -----\n");

    uint32_t serial = 0;
    if (sband_sanity(&serial) != 0) {
        warning("sband: checks failed, not transmitting\n");
        shut_radio_down();
        return;
    }

    uint8_t id_block[SBAND_TX_BLOCK_BYTES];
    build_id_block(id_block);
    const uint32_t pattern_check = checksum_block(id_block, sizeof(id_block));

    sband_record_t record = load_sband_record();
    bool already_stored = record.magic == SBAND_RECORD_MAGIC && record.radio_serial == serial && serial != 0 &&
                          record.pattern_check == pattern_check;
    #ifdef SBAND_TX_FORCE_STORE
    already_stored = false;
    info("sband: SBAND_TX_FORCE_STORE set, storing the packet again\n");
    #endif

    if (already_stored) {
        info("sband: identification packet already in radio %lu, reusing it\n", (unsigned long)serial);
    } else {
        info("sband: storing \"%s\" pattern in radio %lu\n", SBAND_TX_ID_TEXT, (unsigned long)serial);
        if (store_id_packet(id_block) != SUCCESS) {
            warning("sband: could not store the identification packet, not transmitting\n");
            clear_sband_record();
            shut_radio_down();
            return;
        }
        record = (sband_record_t){.magic = SBAND_RECORD_MAGIC, .radio_serial = serial, .pattern_check = pattern_check};
        save_sband_record(&record);
    }

    // Config mode holds every setting below; the PA stays off until Transmit (USM 8.2.1)
    status_t status = stxg2_set_mode(STXG2_MODE_CONFIG);
    if (status == SUCCESS) {
        status = stxg2_configure_encoding(ENCODING_CTRL_RRC_0_20, ENCODING_FRAME_LEN_MAX);
    }
    if (status == SUCCESS) {
        status = stxg2_configure_rf(SBAND_TX_FREQ_KHZ, SBAND_TX_SYM_RATE_KSPS, SBAND_TX_MODCOD, SBAND_TX_POWER_DBM);
    }
    if (status != SUCCESS) {
        warning("sband: could not configure the radio (status %d), not transmitting\n", status);
        shut_radio_down();
        return;
    }

    info("sband: transmitting at %lu kHz, %lu ksps, MODCOD %u (QPSK 1/2), %u dBm, for %lu s of carrier\n",
         (unsigned long)SBAND_TX_FREQ_KHZ, (unsigned long)SBAND_TX_SYM_RATE_KSPS, (unsigned)SBAND_TX_MODCOD,
         (unsigned)SBAND_TX_POWER_DBM, (unsigned long)SBAND_TX_SECONDS);

    // Each pass sends the whole packet, so the last one runs past the limit rather than cutting the callsign short
    const uint32_t target_ms = SBAND_TX_SECONDS * 1000u;
    uint32_t elapsed_ms = 0;
    uint32_t packets_sent = 0;
    while (elapsed_ms < target_ms && packets_sent < SBAND_TX_MAX_PACKETS) {
        status = stxg2_downlink_begin(SBAND_TX_MEM_INDEX, 1);
        if (status != SUCCESS) {
            warning("sband: could not start the downlink (status %d)\n", status);
            break;
        }
        status = wait_packet_sent(&elapsed_ms);
        if (status == SUCCESS) {
            packets_sent++;
        }
        report_transmit(packets_sent);

        status_t end_status = stxg2_downlink_end();
        if (status != SUCCESS) {
            // The radio did not send what we stored, so stop rather than keep the PA on without identification
            warning("sband: stopping, the stored packet did not go out (status %d)\n", status);
            clear_sband_record();
            break;
        }
        if (end_status != SUCCESS) {
            warning("sband: could not end the downlink cleanly (status %d)\n", end_status);
            break;
        }
    }

    info("sband: sent %lu packet(s), %lu ms of carrier\n", (unsigned long)packets_sent, (unsigned long)elapsed_ms);
    shut_radio_down();
}

#else // SBAND_TX_TEST && DEVBUILD

void sband_tx_test(void) {
}

#endif
