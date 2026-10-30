#include "burn_uhf.h"

#include "globals.h"

#if defined(BURN_SEQUENCE) && BURN_SEQUENCE_UHF_MODE != BURN_UHF_MODE_OFF

    #include <hal_delay.h>
    #include <peripheral_clk_config.h>
    #include <stdio.h>
    #include <string.h>

    #include "atmel_start.h"
    #include "burn/uhf_link_packet.h"
    #include "drivers/at86rf215/at86_pvdx.h"
    #include "drivers/at86rf215/at86rf215.h"
    #include "drivers/at86rf215/regs.h"
    #include "logging.h"
    #include "watchdog_driver.h"

    #define UHF_FREQ_HZ 437765000UL
    #define UHF_TX_POWER 10
    #define UHF_BEACON_COUNT 10
    #define UHF_BEACON_PERIOD_MS 1000
    #define UHF_BEACON_TEXT "PVDX post-burnwire UHF beacon"
    #define UHF_LINK_ROUNDS 3

    #define UHF_DATA_TRIES 10
    #define UHF_ACK_TIMEOUT_MS 6000
    #define UHF_DATA_WAIT_MS 120000
    #define UHF_DUP_WINDOW_MS 15000
    #define UHF_ACK_DELAY_MS 2000
    #define UHF_TX_TIMEOUT_MS 200
    #define UHF_STATE_TIMEOUT_MS 50
    #define UHF_WDT_FEED_MS 1000

typedef struct {
    uint32_t tx_frames;
    uint32_t tx_errors;
    uint32_t rx_frames;
    uint32_t rx_fcs_bad;
    uint32_t rx_rejected;
    uint32_t rx_timeouts;
    uint32_t acks_rx;
    uint32_t acks_implicit;
    uint32_t acks_tx;
    uint32_t data_rx;
    uint32_t data_dup_rx;
    uint32_t rounds_ok;
    uint32_t rounds_fail;
    int8_t last_rssi;
    int8_t last_edv;
} uhf_stats_t;

typedef enum {
    UHF_RX_OK = 0,
    UHF_RX_TIMEOUT,
    UHF_RX_FCS_BAD,
    UHF_RX_REJECTED,
    UHF_RX_ERROR,
} uhf_rx_result_t;

typedef enum {
    UHF_A_FAIL = 0,
    UHF_A_ACKED,
    UHF_A_GOT_DATA,
} uhf_a_result_t;

static struct at86rf215 uhf_dev;
static uhf_stats_t uhf_stats;
static uint32_t uhf_our_seq = 0;
static uint32_t uhf_last_data_seq = 0;
static bool uhf_have_last_data = false;
static uint32_t uhf_last_feed = 0;

static uint64_t uhf_cycles(void) {
    static uint32_t last = 0;
    static uint64_t high = 0;
    const uint32_t now = DWT->CYCCNT;
    if (now < last) {
        high += (1ULL << 32);
    }
    last = now;
    return high + now;
}

static uint32_t uhf_ms(void) {
    return (uint32_t)(uhf_cycles() / (CONF_CPU_FREQUENCY / 1000u));
}

static void uhf_time_init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uhf_last_feed = uhf_ms();
}

static void uhf_service(void) {
    const uint32_t now = uhf_ms();
    if (now - uhf_last_feed >= UHF_WDT_FEED_MS) {
        watchdog_feed(WDT);
        uhf_last_feed = now;
    }
}

static void uhf_sleep_ms(uint32_t ms) {
    const uint32_t start = uhf_ms();
    while (uhf_ms() - start < ms) {
        uhf_service();
        delay_us(100);
    }
}

static bool uhf_fe_ok(uint8_t padfe, uint8_t auxs) {
    return padfe == AT86_PADFE_RX && (auxs & AT86_AUXS_EXTLNABYP) == 0;
}

static bool uhf_wait_state(at86rf215_rf_state_t want, uint32_t timeout_ms) {
    const uint32_t start = uhf_ms();
    at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;
    do {
        if (at86rf215_get_state(&uhf_dev, &state, AT86RF215_RF09) == AT86RF215_OK && state == want) {
            return true;
        }
        uhf_service();
        delay_us(20);
    } while (uhf_ms() - start < timeout_ms);
    return false;
}

static bool uhf_txprep(void) {
    if (at86_go_txprep(&uhf_dev) != AT86RF215_OK) {
        return false;
    }
    return uhf_wait_state(AT86RF215_STATE_RF_TXPREP, UHF_STATE_TIMEOUT_MS);
}

static uint8_t uhf_read_irqs(void) {
    return at86_read_irqs(&uhf_dev).bbc;
}

static int uhf_tx(const link_packet_t *pkt) {
    const size_t psdu_len = (size_t)pkt->length + 1;
    int ret = -AT86RF215_INVAL_PARAM;

    if (psdu_len > sizeof(link_packet_t)) {
        goto fail;
    }
    if (!uhf_txprep()) {
        ret = -AT86RF215_TIMEOUT;
        goto fail;
    }
    (void)uhf_read_irqs();
    ret = at86_send_frame(&uhf_dev, (const uint8_t *)pkt, psdu_len);
    if (ret) {
        goto fail;
    }

    const uint32_t start = uhf_ms();
    for (;;) {
        if (uhf_read_irqs() & AT86_BBC_IRQ_TXFE) {
            break;
        }
        if (uhf_ms() - start >= UHF_TX_TIMEOUT_MS) {
            ret = -AT86RF215_TIMEOUT;
            goto fail;
        }
        uhf_service();
    }
    ++uhf_stats.tx_frames;
    uhf_txprep();
    return AT86RF215_OK;

fail:
    ++uhf_stats.tx_errors;
    uhf_txprep();
    return ret;
}

static uhf_rx_result_t uhf_rx(link_packet_t *out, uint32_t timeout_ms) {
    if (!uhf_txprep()) {
        return UHF_RX_ERROR;
    }
    (void)uhf_read_irqs();
    if (at86_go_rx(&uhf_dev) != AT86RF215_OK) {
        uhf_txprep();
        return UHF_RX_ERROR;
    }

    uint32_t start = uhf_ms();
    bool frame_started = false;
    for (;;) {
        const uint8_t irqs = uhf_read_irqs();
        if ((irqs & AT86_BBC_IRQ_RXFS) && !frame_started) {
            frame_started = true;
            uint8_t rssi = 0;
            at86rf215_reg_read_8(&uhf_dev, &rssi, REG_RF09_RSSI);
            uhf_stats.last_rssi = (int8_t)rssi;
            if (uhf_ms() - start + UHF_TX_TIMEOUT_MS > timeout_ms) {
                start = uhf_ms() + UHF_TX_TIMEOUT_MS - timeout_ms;
            }
        }
        if (irqs & AT86_BBC_IRQ_RXFE) {
            break;
        }
        if (uhf_ms() - start >= timeout_ms) {
            ++uhf_stats.rx_timeouts;
            uhf_txprep();
            return UHF_RX_TIMEOUT;
        }
        uhf_service();
    }

    ++uhf_stats.rx_frames;
    uint8_t fll = 0, flh = 0, pc = 0, edv = 0;
    at86rf215_reg_read_8(&uhf_dev, &fll, REG_BBC0_RXFLL);
    at86rf215_reg_read_8(&uhf_dev, &flh, REG_BBC0_RXFLH);
    at86rf215_reg_read_8(&uhf_dev, &pc, REG_BBC0_PC);
    at86rf215_reg_read_8(&uhf_dev, &edv, REG_RF09_EDV);
    uhf_stats.last_edv = (int8_t)edv;
    const size_t frame_len = ((size_t)(flh & 0x07) << 8) | fll;

    if (!(pc & AT86_BBC_PC_FCSOK)) {
        ++uhf_stats.rx_fcs_bad;
        info("uhf: rx FCS_BAD len=%u rssi=%d edv=%d\n", (unsigned)frame_len, (int)uhf_stats.last_rssi, (int)uhf_stats.last_edv);
        return UHF_RX_FCS_BAD;
    }
    if (frame_len < LINK_HEADER_SIZE + LINK_FCS_SIZE || frame_len - LINK_FCS_SIZE > sizeof(link_packet_t)) {
        ++uhf_stats.rx_rejected;
        info("uhf: rx REJECT len=%u (size)\n", (unsigned)frame_len);
        return UHF_RX_REJECTED;
    }
    const size_t psdu_len = frame_len - LINK_FCS_SIZE;
    memset(out, 0, sizeof(*out));
    if (at86rf215_rx_frame(&uhf_dev, AT86RF215_RF09, (uint8_t *)out, psdu_len) != AT86RF215_OK) {
        return UHF_RX_ERROR;
    }
    if (out->length != psdu_len - 1 || LINK_VER(out->ver_ssid) != LINK_VERSION || LINK_SSID(out->ver_ssid) != LINK_SSID_HACKRF ||
        (out->type != LINK_TYPE_DATA && out->type != LINK_TYPE_ACK)) {
        ++uhf_stats.rx_rejected;
        info("uhf: rx REJECT len=%u length=%u ver_ssid=0x%02x type=%u\n", (unsigned)frame_len, (unsigned)out->length,
             (unsigned)out->ver_ssid, (unsigned)out->type);
        return UHF_RX_REJECTED;
    }
    info("uhf: rx %s seq=%lu len=%u call=%.6s rssi=%d edv=%d\n", out->type == LINK_TYPE_ACK ? "ACK" : "DATA", (unsigned long)out->seq,
         (unsigned)frame_len, out->callsign, (int)uhf_stats.last_rssi, (int)uhf_stats.last_edv);
    return UHF_RX_OK;
}

static void uhf_build(link_packet_t *pkt, link_type_t type, uint32_t seq, const char *payload) {
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

static void uhf_send_ack(uint32_t seq) {
    uhf_sleep_ms(UHF_ACK_DELAY_MS);
    link_packet_t ack;
    uhf_build(&ack, LINK_TYPE_ACK, seq, NULL);
    const int ret = uhf_tx(&ack);
    if (ret == AT86RF215_OK) {
        ++uhf_stats.acks_tx;
    }
    info("uhf: tx ACK seq=%lu ret=%d\n", (unsigned long)seq, ret);
}

static bool uhf_is_new_data(const link_packet_t *pkt) {
    return pkt->type == LINK_TYPE_DATA && !(uhf_have_last_data && pkt->seq == uhf_last_data_seq);
}

static void uhf_accept_data(const link_packet_t *pkt) {
    ++uhf_stats.data_rx;
    uhf_last_data_seq = pkt->seq;
    uhf_have_last_data = true;
    uhf_send_ack(pkt->seq);
}

static void uhf_handle_other(const link_packet_t *pkt) {
    if (pkt->type == LINK_TYPE_DATA && uhf_have_last_data && pkt->seq == uhf_last_data_seq) {
        ++uhf_stats.data_dup_rx;
        uhf_send_ack(pkt->seq);
    }
}

static uhf_a_result_t uhf_phase_a(uint32_t round, link_packet_t *data_out) {
    link_packet_t data;
    link_packet_t rx;
    char payload[LINK_MAX_PAYLOAD + 1];
    snprintf(payload, sizeof(payload), "PVDX AT86 round %lu", (unsigned long)round);
    uhf_build(&data, LINK_TYPE_DATA, uhf_our_seq, payload);

    for (unsigned attempt = 1; attempt <= UHF_DATA_TRIES; ++attempt) {
        const int ret = uhf_tx(&data);
        info("uhf: tx DATA seq=%lu try=%u len=%u ret=%d\n", (unsigned long)uhf_our_seq, attempt,
             (unsigned)(data.length + 1 + LINK_FCS_SIZE), ret);
        if (ret != AT86RF215_OK) {
            continue;
        }

        const uint32_t start = uhf_ms();
        while (uhf_ms() - start < UHF_ACK_TIMEOUT_MS) {
            const uint32_t left = UHF_ACK_TIMEOUT_MS - (uhf_ms() - start);
            if (uhf_rx(&rx, left) != UHF_RX_OK) {
                continue;
            }
            if (rx.type == LINK_TYPE_ACK && rx.seq == uhf_our_seq) {
                ++uhf_stats.acks_rx;
                return UHF_A_ACKED;
            }
            if (uhf_is_new_data(&rx)) {
                ++uhf_stats.acks_implicit;
                info("uhf: rx DATA seq=%lu in phase A, taking it as the lost ACK for seq=%lu\n", (unsigned long)rx.seq,
                     (unsigned long)uhf_our_seq);
                *data_out = rx;
                return UHF_A_GOT_DATA;
            }
            uhf_handle_other(&rx);
        }
    }
    return UHF_A_FAIL;
}

static bool uhf_phase_b(const link_packet_t *first) {
    link_packet_t rx;

    if (first == NULL) {
        const uint32_t start = uhf_ms();
        while (first == NULL && uhf_ms() - start < UHF_DATA_WAIT_MS) {
            const uint32_t left = UHF_DATA_WAIT_MS - (uhf_ms() - start);
            if (uhf_rx(&rx, left) != UHF_RX_OK) {
                continue;
            }
            if (uhf_is_new_data(&rx)) {
                first = &rx;
            } else {
                uhf_handle_other(&rx);
            }
        }
        if (first == NULL) {
            return false;
        }
    }
    uhf_accept_data(first);

    uint32_t start = uhf_ms();
    while (uhf_ms() - start < UHF_DUP_WINDOW_MS) {
        const uint32_t left = UHF_DUP_WINDOW_MS - (uhf_ms() - start);
        if (uhf_rx(&rx, left) != UHF_RX_OK || rx.type != LINK_TYPE_DATA) {
            continue;
        }
        if (uhf_is_new_data(&rx)) {
            uhf_accept_data(&rx);
        } else {
            uhf_handle_other(&rx);
        }
        start = uhf_ms();
    }
    return true;
}

    #define UHF_TRY(call)                                                                                                                  \
        do {                                                                                                                               \
            int uhf_ret = (call);                                                                                                          \
            if (uhf_ret != AT86RF215_OK) {                                                                                                 \
                warning("uhf: %s failed (%d)\n", #call, uhf_ret);                                                                          \
                return false;                                                                                                              \
            }                                                                                                                              \
        } while (0)

static bool uhf_setup(void) {
    spi_m_sync_enable(&SPI_UHF);

    UHF_TRY(at86_init_pvdx(&uhf_dev));
    UHF_TRY(at86rf215_conn_check(&uhf_dev));
    UHF_TRY(at86rf215_set_mode(&uhf_dev, AT86RF215_RF_MODE_BBRF));

    const struct at86rf215_radio_conf radio_conf = {
        .cm = AT86RF215_CM_FINE_RES_04,
        .cs = 0,
        .base_freq = 0,
        .lbw = AT86RF215_PLL_LBW_DEFAULT,
    };
    UHF_TRY(at86rf215_radio_conf(&uhf_dev, AT86RF215_RF09, &radio_conf));

    UHF_TRY(change_at86_config(&uhf_dev, AT86_MR_FSK_LINK));
    UHF_TRY(at86rf215_bb_enable(&uhf_dev, AT86RF215_RF09, 1));

    UHF_TRY(at86rf215_set_freq(&uhf_dev, AT86RF215_RF09, UHF_FREQ_HZ));
    UHF_TRY(at86rf215_set_pac(&uhf_dev, AT86RF215_RF09, AT86RF215_PACUR_NO_RED, UHF_TX_POWER));
    UHF_TRY(at86rf215_set_bbc_irq_mask(&uhf_dev, AT86RF215_RF09, AT86_BBC_IRQ_RXFS | AT86_BBC_IRQ_RXFE | AT86_BBC_IRQ_TXFE));

    uint8_t auxs = 0;
    uint8_t padfe = 0;
    UHF_TRY(at86rf215_reg_read_8(&uhf_dev, &padfe, REG_RF09_PADFE));
    UHF_TRY(at86rf215_reg_read_8(&uhf_dev, &auxs, REG_RF09_AUXS));
    if (!uhf_fe_ok(padfe, auxs)) {
        warning("uhf: bad front end padfe=0x%02x auxs=0x%02x\n", padfe, auxs);
        return false;
    }

    if (!uhf_txprep()) {
        warning("uhf: radio did not reach TXPREP\n");
        return false;
    }
    at86rf215_pll_ls_t pll = AT86RF215_PLL_UNLOCKED;
    UHF_TRY(at86rf215_get_pll_ls(&uhf_dev, &pll, AT86RF215_RF09));
    if (pll != AT86RF215_PLL_LOCKED) {
        warning("uhf: PLL did not lock\n");
        return false;
    }
    return true;
}

static bool uhf_start(void) {
    memset(&uhf_stats, 0, sizeof(uhf_stats));
    uhf_time_init();
    info("uhf: setting up MR-FSK radio (%lu Hz, power %d, call sign %s)\n", (unsigned long)UHF_FREQ_HZ, UHF_TX_POWER, LINK_CALLSIGN);
    if (!uhf_setup()) {
        warning("uhf: setup failed, skipping the UHF step\n");
        at86rf215_set_cmd(&uhf_dev, AT86RF215_CMD_RF_TRXOFF, AT86RF215_RF09);
        return false;
    }
    return true;
}

static void uhf_stop(void) {
    at86rf215_set_cmd(&uhf_dev, AT86RF215_CMD_RF_TRXOFF, AT86RF215_RF09);
    info("uhf: tx=%lu txerr=%lu rx=%lu fcsbad=%lu rej=%lu rxto=%lu ackrx=%lu ackimp=%lu acktx=%lu datarx=%lu dup=%lu, radio off\n",
         (unsigned long)uhf_stats.tx_frames, (unsigned long)uhf_stats.tx_errors, (unsigned long)uhf_stats.rx_frames,
         (unsigned long)uhf_stats.rx_fcs_bad, (unsigned long)uhf_stats.rx_rejected, (unsigned long)uhf_stats.rx_timeouts,
         (unsigned long)uhf_stats.acks_rx, (unsigned long)uhf_stats.acks_implicit, (unsigned long)uhf_stats.acks_tx,
         (unsigned long)uhf_stats.data_rx, (unsigned long)uhf_stats.data_dup_rx);
}

void burn_uhf_beacon(void) {
    if (!uhf_start()) {
        return;
    }
    link_packet_t pkt;
    uint32_t sent = 0;
    for (uint32_t n = 0; n < UHF_BEACON_COUNT; n++) {
        uhf_build(&pkt, LINK_TYPE_DATA, n, UHF_BEACON_TEXT);
        const int ret = uhf_tx(&pkt);
        if (ret == AT86RF215_OK) {
            sent++;
        }
        info("uhf: beacon seq=%lu ret=%d\n", (unsigned long)n, ret);
        uhf_sleep_ms(UHF_BEACON_PERIOD_MS);
    }
    info("uhf: sent %lu/%d beacons\n", (unsigned long)sent, UHF_BEACON_COUNT);
    uhf_stop();
}

void burn_uhf_link(void) {
    if (!uhf_start()) {
        return;
    }
    for (uint32_t round = 0; round < UHF_LINK_ROUNDS; round++) {
        link_packet_t early_data;
        const uhf_a_result_t a = uhf_phase_a(round, &early_data);
        const bool a_ok = a != UHF_A_FAIL;
        const bool b_ok = a_ok && uhf_phase_b(a == UHF_A_GOT_DATA ? &early_data : NULL);
        if (a_ok && b_ok) {
            ++uhf_stats.rounds_ok;
        } else {
            ++uhf_stats.rounds_fail;
        }
        info("uhf: round %lu a=%s b=%s ok=%lu fail=%lu\n", (unsigned long)round, a_ok ? "ok" : "FAIL", b_ok ? "ok" : "FAIL",
             (unsigned long)uhf_stats.rounds_ok, (unsigned long)uhf_stats.rounds_fail);
        ++uhf_our_seq;
    }
    uhf_stop();
}

#endif // BURN_SEQUENCE
