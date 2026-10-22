/**
 * test_at86_link.c
 *
 * Two-way MR-FSK link test between the AT86RF215 (RF09) and a HackRF running scripts/hackrf_link.py.
 *
 * Each round:
 *   Phase A: the AT86 sends DATA(seq) and listens for ACK(seq). It sends again if no ACK comes.
 *   Phase B: the AT86 listens for DATA from the HackRF, sends ACK(seq), then listens a little longer and sends the
 *            ACK again for every repeat of the same DATA (the first ACK can be lost).
 *
 * The AT86 sends an ACK for every HackRF DATA it receives, in any phase. The HackRF sends new DATA only after it
 * received our DATA, so new DATA during phase A counts as the missing ACK: phase B starts with it. Without this rule,
 * a lost ACK leaves both sides sending DATA and waiting for an ACK that never comes.
 *
 * Timing: one HackRF cannot transmit and receive at the same time. It closes and opens the device over USB for each
 * switch, which takes about a second. The ACK delay and the listen windows below leave room for that.
 *
 * Front end: FEA09 enables the GRF5504 PA. PADFE configuration 1 drives FEA09 high only in the TX state. The external
 * LNA is always on, so AUXS.EXTLNABYP stays 0 (in configuration 1 the "RX bypass" mode would drive FEA09 high in RX).
 *
 * The test runs before the scheduler, so it polls BBC0_IRQS and uses the DWT cycle counter for time.
 * It prints an "at86hb" heartbeat line every LINK_HEARTBEAT_MS and keeps live counters in at86_link_status.
 * scripts/at86_selftest.py reads both to check that the radio and the test loop keep running.
 *
 * Build: make test TESTS="at86_link"
 */

#include "test_at86_link.h"

#ifdef TEST_AT86_LINK

    #include <driver_init.h>
    #include <hal_delay.h>
    #include <peripheral_clk_config.h>
    #include <stdio.h>
    #include <string.h>

    #include "drivers/at86rf215/at86rf215.h"
    #include "drivers/at86rf215/regs.h"
    #include "drivers/watchdog/watchdog_driver.h"
    #include "logging.h"
    #include "tests/at86_link_packet.h"
    #include "tests/test.h"

/* ---------- Configuration ---------- */

    #define LINK_FREQ_HZ 437765000UL
    #define LINK_TX_POWER 0 // RFn_PAC.TXPWR, 0..31. The GRF5504 adds gain after this, so start low and raise it
    #define LINK_ROUNDS 0   // 0 runs forever. Any other value runs that many rounds and then returns

    #define LINK_DATA_TRIES 10       // DATA sends per round before phase A gives up
    #define LINK_ACK_TIMEOUT_MS 6000 // Listen time for an ACK. Longer than the HackRF RX to TX turnaround (its "turnaround=" log)
    #define LINK_DATA_WAIT_MS 120000 // Listen time for HackRF DATA in phase B. Longer than the HackRF dup window plus its DATA tries
    #define LINK_DUP_WINDOW_MS 15000 // Listen time for repeated HackRF DATA after our ACK. Longer than the HackRF retry period (about 8 s)
    #define LINK_ACK_DELAY_MS 2000   // Wait before each ACK so the HackRF can close TX and open RX (its "rxready=" log)
    #define LINK_TX_TIMEOUT_MS 200   // Time for one frame to finish (a full frame is about 10 ms at 50 kb/s)
    #define LINK_STATE_TIMEOUT_MS 50 // Time for a state change (TXPREP, RX)
    #define LINK_HEARTBEAT_MS 2000   // Heartbeat line period
    #define LINK_WDT_FEED_MS 1000    // Hardware watchdog feed period (only if it is enabled)

    #define LINK_SFD0 0x7209 // Uncoded IEEE 802.15.4g SFD (0x904E in air order, LSB first). Reset value
    #define LINK_SFD1 0x72F6 // Coded SFD. Reset value. Not used because FEC is off

    #define BBC_IRQ_RXFS (1u << 0)
    #define BBC_IRQ_RXFE (1u << 1)
    #define BBC_IRQ_TXFE (1u << 4)
    #define BBC_PC_FCSOK (1u << 5)
    #define AUXS_EXTLNABYP (1u << 7)

    #define AT86RF215_PN_RF215 0x34
    #define AT86RF215_PN_RF215IQ 0x35
    #define AT86RF215_PN_RF215M 0x36

volatile at86_link_status_t at86_link_status;
    #define ST at86_link_status

static struct at86rf215 link_dev;

// A frame buffer burst: 2 byte SPI header, the PSDU, and 2 bytes for the FCS that the chip fills in
static uint8_t link_burst[2 + sizeof(link_packet_t) + LINK_FCS_SIZE];

static uint32_t our_seq = 0;        // Sequence number of our DATA
static uint32_t last_data_seq = 0;  // Last HackRF DATA sequence number we accepted
static bool have_last_data = false; // True after the first HackRF DATA

typedef enum {
    LINK_RX_OK = 0,
    LINK_RX_TIMEOUT,
    LINK_RX_FCS_BAD,
    LINK_RX_REJECTED,
    LINK_RX_ERROR,
} link_rx_result_t;

/* ---------- Time and housekeeping ---------- */

/**
 * \fn link_cycles
 *
 * \brief 64-bit DWT cycle count. CYCCNT wraps every 358 s at 12 MHz, so this must be called more often than that.
 *      link_service() calls it on every poll
 */
static uint64_t link_cycles(void) {
    static uint32_t last = 0;
    static uint64_t high = 0;
    const uint32_t now = DWT->CYCCNT;
    if (now < last) {
        high += (1ULL << 32);
    }
    last = now;
    return high + now;
}

static uint32_t link_ms(void) {
    return (uint32_t)(link_cycles() / (CONF_CPU_FREQUENCY / 1000u));
}

static void link_time_init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static bool link_valid_pn(uint8_t pn) {
    return pn == AT86RF215_PN_RF215 || pn == AT86RF215_PN_RF215IQ || pn == AT86RF215_PN_RF215M;
}

static bool link_fe_ok(uint8_t padfe, uint8_t auxs) {
    return ((padfe >> 6) & 0x3) == AT86RF215_RF_FEMODE1 && (auxs & AUXS_EXTLNABYP) == 0;
}

/**
 * \fn link_heartbeat
 *
 * \brief Reads the part number, state and front end registers over SPI and prints one "at86hb" line.
 *      scripts/at86_selftest.py parses this line, so keep the key=value format
 */
static void link_heartbeat(void) {
    uint8_t pn = 0, state = 0, padfe = 0, auxs = 0;
    ++ST.spi_checks;
    int ret = at86rf215_reg_read_8(&link_dev, &pn, REG_RF_PN);
    if (ret != AT86RF215_OK || pn != ST.part_number) {
        ++ST.spi_errors;
        ST.last_error = ret ? ret : -1;
    }
    at86rf215_reg_read_8(&link_dev, &state, REG_RF09_STATE);
    at86rf215_reg_read_8(&link_dev, &padfe, REG_RF09_PADFE);
    at86rf215_reg_read_8(&link_dev, &auxs, REG_RF09_AUXS);
    ST.rf_state = state & 0x7;
    ST.padfe = padfe;
    ST.auxs = auxs;
    if (!link_fe_ok(padfe, auxs)) {
        ++ST.fe_errors;
    }
    ++ST.heartbeats;

    test_log(
        "at86hb n=%lu t=%lu ph=%lu rnd=%lu polls=%lu pn=0x%02x st=%u spichk=%lu spierr=%lu padfe=0x%02x auxs=0x%02x "
        "feerr=%lu\n",
        ST.heartbeats, ST.uptime_ms, ST.phase, ST.round, ST.polls, pn, ST.rf_state, ST.spi_checks, ST.spi_errors, padfe, auxs,
        ST.fe_errors);
    test_log(
        "at86hb2 n=%lu tx=%lu txerr=%lu rxl=%lu rx=%lu fcsbad=%lu rej=%lu rxto=%lu ackrx=%lu acktx=%lu datarx=%lu "
        "dup=%lu ackimp=%lu ok=%lu fail=%lu rssi=%d err=%d\n",
        ST.heartbeats, ST.tx_frames, ST.tx_errors, ST.rx_listens, ST.rx_frames, ST.rx_fcs_bad, ST.rx_rejected, ST.rx_timeouts, ST.acks_rx,
        ST.acks_tx, ST.data_rx, ST.data_dup_rx, ST.acks_implicit, ST.rounds_ok, ST.rounds_fail, (int)ST.last_rssi, (int)ST.last_error);
}

/**
 * \fn link_service
 *
 * \brief Called from every wait loop: updates the uptime, feeds the hardware watchdog if it runs, and prints the
 *      heartbeat when it is due
 */
static void link_service(void) {
    static uint32_t last_feed = 0;
    static uint32_t last_heartbeat = 0;

    ++ST.polls;
    const uint32_t now = link_ms();
    ST.uptime_ms = now;

    if ((WDT->CTRLA.reg & (WDT_CTRLA_ENABLE | WDT_CTRLA_ALWAYSON)) && now - last_feed >= LINK_WDT_FEED_MS) {
        watchdog_feed(WDT);
        last_feed = now;
    }
    if (now - last_heartbeat >= LINK_HEARTBEAT_MS) {
        last_heartbeat = now;
        link_heartbeat();
    }
}

/* ---------- Radio helpers ---------- */

static bool link_wait_state(at86rf215_rf_state_t want, uint32_t timeout_ms) {
    const uint32_t start = link_ms();
    at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;
    do {
        if (at86rf215_get_state(&link_dev, &state, AT86RF215_RF09) == AT86RF215_OK && state == want) {
            return true;
        }
        link_service();
        delay_us(20);
    } while (link_ms() - start < timeout_ms);
    return false;
}

static bool link_txprep(void) {
    if (at86rf215_set_cmd(&link_dev, AT86RF215_CMD_RF_TXPREP, AT86RF215_RF09) != AT86RF215_OK) {
        return false;
    }
    return link_wait_state(AT86RF215_STATE_RF_TXPREP, LINK_STATE_TIMEOUT_MS);
}

static uint8_t link_read_irqs(void) {
    uint8_t irqs = 0;
    at86rf215_reg_read_8(&link_dev, &irqs, REG_BBC0_IRQS); // Reading clears the flags
    return irqs;
}

/**
 * \fn link_tx
 *
 * \brief Sends one packet: TXPREP, frame length, frame buffer burst, CMD TX, wait for TXFE, back to TXPREP.
 *      FEA09 (the GRF5504 enable) is high only between CMD TX and the return to TXPREP
 */
static int link_tx(const link_packet_t *pkt) {
    const size_t psdu_len = (size_t)pkt->length + 1;
    const size_t frame_len = psdu_len + LINK_FCS_SIZE; // TXFL covers the FCS that the chip appends (PC.TXAFCS)
    int ret = -AT86RF215_INVAL_PARAM;

    if (psdu_len > sizeof(link_packet_t)) {
        goto fail;
    }
    if (!link_txprep()) {
        ret = -AT86RF215_TIMEOUT;
        goto fail;
    }
    ret = at86rf215_reg_write_8(&link_dev, frame_len & 0xFF, REG_BBC0_TXFLL);
    if (ret)
        goto fail;
    ret = at86rf215_reg_write_8(&link_dev, (frame_len >> 8) & 0x07, REG_BBC0_TXFLH);
    if (ret)
        goto fail;

    // at86rf215_spi_write() drives chip select itself, so the address and the data must go out in one call
    link_burst[0] = (REG_BBC0_FBTXS >> 8) | 0x80;
    link_burst[1] = REG_BBC0_FBTXS & 0xFF;
    memcpy(&link_burst[2], pkt, psdu_len);
    memset(&link_burst[2 + psdu_len], 0, LINK_FCS_SIZE);
    ret = at86rf215_spi_write(&link_dev, link_burst, 2 + frame_len);
    if (ret)
        goto fail;

    (void)link_read_irqs();
    ret = at86rf215_set_cmd(&link_dev, AT86RF215_CMD_RF_TX, AT86RF215_RF09);
    if (ret)
        goto fail;

    const uint32_t start = link_ms();
    for (;;) {
        if (link_read_irqs() & BBC_IRQ_TXFE) {
            break;
        }
        if (link_ms() - start >= LINK_TX_TIMEOUT_MS) {
            ret = -AT86RF215_TIMEOUT;
            goto fail;
        }
        link_service();
    }
    ++ST.tx_frames;
    link_txprep(); // The chip returns to TXPREP by itself after TX. This makes sure of it
    return AT86RF215_OK;

fail:
    ++ST.tx_errors;
    ST.last_error = ret;
    link_txprep();
    return ret;
}

/**
 * \fn link_rx
 *
 * \brief Listens for one frame for up to \p timeout_ms. The chip goes to TXPREP by itself after RXFE.
 *
 * \return LINK_RX_OK with a validated packet in \p out, or the reason it failed
 */
static link_rx_result_t link_rx(link_packet_t *out, uint32_t timeout_ms) {
    if (!link_txprep()) {
        ST.last_error = -AT86RF215_TIMEOUT;
        return LINK_RX_ERROR;
    }
    (void)link_read_irqs();
    if (at86rf215_set_cmd(&link_dev, AT86RF215_CMD_RF_RX, AT86RF215_RF09) != AT86RF215_OK) {
        return LINK_RX_ERROR;
    }
    ++ST.rx_listens;

    uint32_t start = link_ms();
    bool frame_started = false;
    for (;;) {
        const uint8_t irqs = link_read_irqs();
        if ((irqs & BBC_IRQ_RXFS) && !frame_started) {
            // A frame started. Give it time to finish even if the window ends now
            frame_started = true;
            uint8_t rssi = 0;
            at86rf215_reg_read_8(&link_dev, &rssi, REG_RF09_RSSI);
            ST.last_rssi = (int8_t)rssi;
            if (link_ms() - start + LINK_TX_TIMEOUT_MS > timeout_ms) {
                start = link_ms() + LINK_TX_TIMEOUT_MS - timeout_ms;
            }
        }
        if (irqs & BBC_IRQ_RXFE) {
            break;
        }
        if (link_ms() - start >= timeout_ms) {
            ++ST.rx_timeouts;
            link_txprep();
            return LINK_RX_TIMEOUT;
        }
        link_service();
    }

    ++ST.rx_frames;
    uint8_t fll = 0, flh = 0, pc = 0, edv = 0;
    at86rf215_reg_read_8(&link_dev, &fll, REG_BBC0_RXFLL);
    at86rf215_reg_read_8(&link_dev, &flh, REG_BBC0_RXFLH);
    at86rf215_reg_read_8(&link_dev, &pc, REG_BBC0_PC);
    at86rf215_reg_read_8(&link_dev, &edv, REG_RF09_EDV);
    ST.last_edv = (int8_t)edv;
    const size_t frame_len = ((size_t)(flh & 0x07) << 8) | fll;

    if (!(pc & BBC_PC_FCSOK)) {
        ++ST.rx_fcs_bad;
        test_log("at86pkt rx FCS_BAD len=%u rssi=%d edv=%d\n", (unsigned)frame_len, (int)ST.last_rssi, (int)ST.last_edv);
        return LINK_RX_FCS_BAD;
    }
    if (frame_len < LINK_HEADER_SIZE + LINK_FCS_SIZE || frame_len - LINK_FCS_SIZE > sizeof(link_packet_t)) {
        ++ST.rx_rejected;
        test_log("at86pkt rx REJECT len=%u (size)\n", (unsigned)frame_len);
        return LINK_RX_REJECTED;
    }
    const size_t psdu_len = frame_len - LINK_FCS_SIZE;
    memset(out, 0, sizeof(*out));
    if (at86rf215_rx_frame(&link_dev, AT86RF215_RF09, (uint8_t *)out, psdu_len) != AT86RF215_OK) {
        ST.last_error = -1;
        return LINK_RX_ERROR;
    }
    if (out->length != psdu_len - 1 || LINK_VER(out->ver_ssid) != LINK_VERSION || LINK_SSID(out->ver_ssid) != LINK_SSID_HACKRF ||
        (out->type != LINK_TYPE_DATA && out->type != LINK_TYPE_ACK)) {
        ++ST.rx_rejected;
        test_log("at86pkt rx REJECT len=%u length=%u ver_ssid=0x%02x type=%u\n", (unsigned)frame_len, out->length, out->ver_ssid,
                 out->type);
        return LINK_RX_REJECTED;
    }
    test_log("at86pkt rx %s seq=%lu len=%u call=%c%c%c%c%c%c rssi=%d edv=%d\n", out->type == LINK_TYPE_ACK ? "ACK" : "DATA", out->seq,
             (unsigned)frame_len, out->callsign[0], out->callsign[1], out->callsign[2], out->callsign[3], out->callsign[4],
             out->callsign[5], (int)ST.last_rssi, (int)ST.last_edv);
    return LINK_RX_OK;
}

static void link_build(link_packet_t *pkt, link_type_t type, uint32_t seq, const char *payload) {
    memset(pkt, 0, sizeof(*pkt));
    const size_t payload_len = payload ? strnlen(payload, LINK_MAX_PAYLOAD) : 0;
    pkt->length = LINK_LENGTH_FIELD(payload_len);
    memcpy(pkt->callsign, LINK_CALLSIGN, sizeof(pkt->callsign));
    pkt->ver_ssid = LINK_VER_SSID(LINK_VERSION, LINK_SSID_AT86);
    pkt->seq = seq;
    pkt->type = (uint8_t)type;
    if (payload_len) {
        memcpy(pkt->payload, payload, payload_len);
    }
}

static void link_sleep_ms(uint32_t ms) {
    const uint32_t start = link_ms();
    while (link_ms() - start < ms) {
        link_service();
        delay_us(100);
    }
}

static void link_send_ack(uint32_t seq) {
    link_sleep_ms(LINK_ACK_DELAY_MS);
    link_packet_t ack;
    link_build(&ack, LINK_TYPE_ACK, seq, NULL);
    const int ret = link_tx(&ack);
    if (ret == AT86RF215_OK) {
        ++ST.acks_tx;
    }
    test_log("at86pkt tx ACK seq=%lu ret=%d\n", seq, ret);
}

static bool link_is_new_data(const link_packet_t *pkt) {
    return pkt->type == LINK_TYPE_DATA && !(have_last_data && pkt->seq == last_data_seq);
}

/**
 * \fn link_accept_data
 *
 * \brief Accepts new HackRF DATA: counts it, remembers its sequence number and sends the ACK
 */
static void link_accept_data(const link_packet_t *pkt) {
    ++ST.data_rx;
    last_data_seq = pkt->seq;
    have_last_data = true;
    ST.phase = LINK_PHASE_B_TX_ACK;
    link_send_ack(pkt->seq);
}

/**
 * \fn link_handle_other
 *
 * \brief Handles a valid packet that the current phase does not wait for. A repeat of the last HackRF DATA means the
 *      HackRF missed our ACK, so ACK it again. New DATA is for the caller (see link_is_new_data)
 */
static void link_handle_other(const link_packet_t *pkt) {
    if (pkt->type == LINK_TYPE_DATA && have_last_data && pkt->seq == last_data_seq) {
        ++ST.data_dup_rx;
        link_send_ack(pkt->seq);
    }
}

/* ---------- Exchange ---------- */

typedef enum {
    LINK_A_FAIL = 0,
    LINK_A_ACKED,    // ACK(seq) received
    LINK_A_GOT_DATA, // New HackRF DATA received instead: the HackRF has our DATA, and phase B starts with its DATA
} link_a_result_t;

/**
 * \fn link_phase_a
 *
 * \param data_out Receives the HackRF DATA when the result is LINK_A_GOT_DATA
 */
static link_a_result_t link_phase_a(link_packet_t *data_out) {
    link_packet_t data;
    link_packet_t rx;
    char payload[LINK_MAX_PAYLOAD + 1];
    snprintf(payload, sizeof(payload), "PVDX AT86 round %lu", (unsigned long)ST.round);
    link_build(&data, LINK_TYPE_DATA, our_seq, payload);

    for (unsigned attempt = 1; attempt <= LINK_DATA_TRIES; ++attempt) {
        ST.phase = LINK_PHASE_A_TX_DATA;
        const int ret = link_tx(&data);
        test_log("at86pkt tx DATA seq=%lu try=%u len=%u ret=%d\n", our_seq, attempt, (unsigned)(data.length + 1 + LINK_FCS_SIZE), ret);
        if (ret != AT86RF215_OK) {
            continue;
        }

        ST.phase = LINK_PHASE_A_WAIT_ACK;
        const uint32_t start = link_ms();
        while (link_ms() - start < LINK_ACK_TIMEOUT_MS) {
            const uint32_t left = LINK_ACK_TIMEOUT_MS - (link_ms() - start);
            if (link_rx(&rx, left) != LINK_RX_OK) {
                continue;
            }
            if (rx.type == LINK_TYPE_ACK && rx.seq == our_seq) {
                ++ST.acks_rx;
                return LINK_A_ACKED;
            }
            if (link_is_new_data(&rx)) {
                ++ST.acks_implicit;
                test_log("at86pkt rx DATA seq=%lu in phase A: the HackRF has our DATA seq=%lu, its ACK was lost\n", rx.seq, our_seq);
                *data_out = rx;
                return LINK_A_GOT_DATA;
            }
            link_handle_other(&rx);
        }
    }
    return LINK_A_FAIL;
}

/**
 * \fn link_phase_b
 *
 * \param first HackRF DATA that already arrived in phase A, or NULL to listen for it
 */
static bool link_phase_b(const link_packet_t *first) {
    link_packet_t rx;

    if (first == NULL) {
        ST.phase = LINK_PHASE_B_WAIT_DATA;
        const uint32_t start = link_ms();
        while (first == NULL && link_ms() - start < LINK_DATA_WAIT_MS) {
            const uint32_t left = LINK_DATA_WAIT_MS - (link_ms() - start);
            if (link_rx(&rx, left) != LINK_RX_OK) {
                continue;
            }
            if (link_is_new_data(&rx)) {
                first = &rx;
            } else {
                link_handle_other(&rx); // Old DATA again, or a repeated ACK for phase A
            }
        }
        if (first == NULL) {
            return false;
        }
    }
    link_accept_data(first);

    // Stay in RX a little longer in case the HackRF did not hear the ACK and sends the same DATA again
    ST.phase = LINK_PHASE_B_DUP_WINDOW;
    uint32_t start = link_ms();
    while (link_ms() - start < LINK_DUP_WINDOW_MS) {
        const uint32_t left = LINK_DUP_WINDOW_MS - (link_ms() - start);
        if (link_rx(&rx, left) != LINK_RX_OK || rx.type != LINK_TYPE_DATA) {
            continue;
        }
        if (link_is_new_data(&rx)) {
            link_accept_data(&rx); // Not expected here, but every DATA gets an ACK
            ST.phase = LINK_PHASE_B_DUP_WINDOW;
        } else {
            link_handle_other(&rx);
        }
        start = link_ms(); // Give the next ACK the same window
    }
    return true;
}

/* ---------- Setup ---------- */

static int link_setup(void) {
    spi_m_sync_enable(&SPI_UHF);

    link_dev.clko_os = AT86RF215_RF_CLKO_OFF;
    link_dev.clk_drv = AT86RF215_RF_DRVCLKO2;
    link_dev.rf_femode_09 = AT86RF215_RF_FEMODE1; // FEA09 high only in TX: GRF5504 enable
    link_dev.rf_femode_24 = AT86RF215_RF_FEMODE0;
    link_dev.xo_fs = 0;
    link_dev.xo_trim = 0;
    link_dev.irqmm = 0;
    link_dev.irqp = 0;
    link_dev.pad_drv = AT86RF215_RF_DRV2;

    int ret = at86rf215_init(&link_dev);
    if (ret) {
        // Raw part number byte: 0x00 usually means no MISO or no radio power, 0xFF means MISO stuck high
        uint8_t raw_pn = 0, raw_vn = 0;
        at86rf215_reg_read_8(&link_dev, &raw_pn, REG_RF_PN);
        at86rf215_reg_read_8(&link_dev, &raw_vn, REG_RF_VN);
        test_log("at86link init FAIL step=init ret=%d raw_pn=0x%02x raw_vn=0x%02x\n", ret, raw_pn, raw_vn);
        return ret;
    }
    ret = at86rf215_conn_check(&link_dev);
    uint8_t pn = 0, vn = 0;
    at86rf215_reg_read_8(&link_dev, &pn, REG_RF_PN);
    at86rf215_reg_read_8(&link_dev, &vn, REG_RF_VN);
    ST.part_number = pn;
    ST.version = vn;
    test_log("at86link init pn=0x%02x vn=0x%02x conn=%d\n", pn, vn, ret);
    if (ret || !link_valid_pn(pn)) {
        return ret ? ret : -1;
    }

    ret = at86rf215_set_mode(&link_dev, AT86RF215_RF_MODE_BBRF);
    if (ret)
        goto out;

    const struct at86rf215_radio_conf radio_conf = {
        .cm = AT86RF215_CM_FINE_RES_04,
        .cs = 0,
        .base_freq = 0,
        .lbw = AT86RF215_PLL_LBW_DEFAULT,
    };
    ret = at86rf215_radio_conf(&link_dev, AT86RF215_RF09, &radio_conf);
    if (ret)
        goto out;

    struct at86rf215_bb_conf bb;
    memset(&bb, 0, sizeof(bb));
    bb.pt = AT86RF215_BB_MRFSK;
    bb.fcst = AT86RF215_FCS_16;
    bb.txafcs = 1;
    bb.fcsfe = 0; // Keep frames with a bad FCS so the test can count them
    bb.fsk.mord = AT86RF215_2FSK;
    bb.fsk.midx = AT86RF215_MIDX_3; // h = 1.0, deviation +-25 kHz at 50 ksym/s
    bb.fsk.midxs = AT86RF215_MIDXS_88;
    bb.fsk.bt = AT86RF215_FSK_BT_10;
    bb.fsk.srate = AT86RF215_FSK_SRATE_50;
    bb.fsk.fi = 0;
    bb.fsk.preamble_length = 8;
    bb.fsk.rxo = AT86RF215_FSK_RXO_18DB; // Reset value
    bb.fsk.fecie = 1;                    // Reset value. FEC is off, so this has no effect
    bb.fsk.fecs = AT86RF215_FSK_FEC_NRNSC;
    bb.fsk.sfd_threshold = 8;      // Reset value
    bb.fsk.preamble_threshold = 5; // Reset value
    bb.fsk.rawrbit = 1;            // Reset value
    bb.fsk.csfd0 = AT86RF215_SFD_UNCODED_IEEE;
    bb.fsk.csfd1 = AT86RF215_SFD_CODED_IEEE;
    bb.fsk.sfd0 = LINK_SFD0;
    bb.fsk.sfd1 = LINK_SFD1;
    bb.fsk.sfd = 0; // Send SFD0: uncoded
    bb.fsk.dw = 0;  // No data whitening
    ret = at86rf215_bb_conf(&link_dev, AT86RF215_RF09, &bb);
    if (ret)
        goto out;
    ret = at86rf215_bb_enable(&link_dev, AT86RF215_RF09, 1);
    if (ret)
        goto out;

    ret = at86rf215_set_freq(&link_dev, AT86RF215_RF09, LINK_FREQ_HZ);
    if (ret)
        goto out;
    ret = at86rf215_set_pac(&link_dev, AT86RF215_RF09, AT86RF215_PACUR_NO_RED, LINK_TX_POWER);
    if (ret)
        goto out;
    ret = at86rf215_set_bbc_irq_mask(&link_dev, AT86RF215_RF09, BBC_IRQ_RXFS | BBC_IRQ_RXFE | BBC_IRQ_TXFE);
    if (ret)
        goto out;

    // The LNA cannot be bypassed. With EXTLNABYP = 1, PADFE configuration 1 would drive FEA09 (PA enable) high in RX
    uint8_t auxs = 0;
    ret = at86rf215_reg_read_8(&link_dev, &auxs, REG_RF09_AUXS);
    if (ret)
        goto out;
    if (auxs & AUXS_EXTLNABYP) {
        ret = at86rf215_reg_write_8(&link_dev, auxs & ~AUXS_EXTLNABYP, REG_RF09_AUXS);
        if (ret)
            goto out;
    }

    uint8_t padfe = 0;
    at86rf215_reg_read_8(&link_dev, &padfe, REG_RF09_PADFE);
    at86rf215_reg_read_8(&link_dev, &auxs, REG_RF09_AUXS);
    ST.padfe = padfe;
    ST.auxs = auxs;
    test_log("at86link fe padfe=0x%02x auxs=0x%02x fe=%s\n", padfe, auxs, link_fe_ok(padfe, auxs) ? "ok" : "BAD");
    if (!link_fe_ok(padfe, auxs)) {
        ret = -1;
        goto out;
    }

    uint8_t sfd0l = 0, sfd0h = 0, phrtx = 0, pc = 0;
    at86rf215_reg_read_8(&link_dev, &sfd0l, REG_BBC0_FSKSFD0L);
    at86rf215_reg_read_8(&link_dev, &sfd0h, REG_BBC0_FSKSFD0H);
    at86rf215_reg_read_8(&link_dev, &phrtx, REG_BBC0_FSKPHRTX);
    at86rf215_reg_read_8(&link_dev, &pc, REG_BBC0_PC);
    test_log("at86link phy freq=%lu sfd0=0x%02x%02x phrtx=0x%02x pc=0x%02x txpwr=%d\n", (unsigned long)LINK_FREQ_HZ, sfd0h, sfd0l, phrtx,
             pc, LINK_TX_POWER);

    if (!link_txprep()) {
        ret = -AT86RF215_TIMEOUT;
        goto out;
    }
    at86rf215_pll_ls_t pll = AT86RF215_PLL_UNLOCKED;
    ret = at86rf215_get_pll_ls(&link_dev, &pll, AT86RF215_RF09);
    ST.pll_locked = (ret == AT86RF215_OK && pll == AT86RF215_PLL_LOCKED);
    test_log("at86link pll locked=%u\n", ST.pll_locked);
    if (!ST.pll_locked) {
        ret = -1;
    }

out:
    if (ret) {
        test_log("at86link init FAIL ret=%d\n", ret);
    }
    return ret;
}

static bool link_more_rounds(uint32_t round) {
    const uint32_t rounds = LINK_ROUNDS;
    return rounds == 0 || round < rounds;
}

void test_at86_link(void) {
    memset((void *)&at86_link_status, 0, sizeof(at86_link_status));
    ST.magic = AT86_LINK_STATUS_MAGIC;
    ST.phase = LINK_PHASE_INIT;
    link_time_init();

    test_log("at86link start rounds=%u hb_ms=%u\n", (unsigned)LINK_ROUNDS, (unsigned)LINK_HEARTBEAT_MS);
    const int ret = link_setup();
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "at86 link setup\n");
    if (ret != AT86RF215_OK) {
        ST.phase = LINK_PHASE_INIT_FAILED;
        ST.last_error = ret;
        return;
    }
    test_log("at86link ready\n");

    for (ST.round = 0; link_more_rounds(ST.round); ++ST.round) {
        link_packet_t early_data;
        const link_a_result_t a = link_phase_a(&early_data);
        const bool a_ok = a != LINK_A_FAIL;
        const bool b_ok = a_ok && link_phase_b(a == LINK_A_GOT_DATA ? &early_data : NULL);
        if (a_ok && b_ok) {
            ++ST.rounds_ok;
        } else {
            ++ST.rounds_fail;
        }
        test_log("at86round n=%lu a=%s b=%s ok=%lu fail=%lu\n", ST.round, a_ok ? "ok" : "FAIL", b_ok ? "ok" : "FAIL", ST.rounds_ok,
                 ST.rounds_fail);
        ++our_seq;
    }

    ST.phase = LINK_PHASE_DONE;
    test_log("at86link done ok=%lu fail=%lu\n", ST.rounds_ok, ST.rounds_fail);
    PVDX_ASSERT_MSG(ST.rounds_fail == 0, "at86 link: every round completed\n");
}

#endif // TEST_AT86_LINK
