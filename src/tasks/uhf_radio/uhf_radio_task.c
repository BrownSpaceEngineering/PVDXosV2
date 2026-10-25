#include "uhf_radio_task.h"

#include <string.h>

#include "at86rf215.h"
#include "logging.h"
#include "regs.h"

uint8_t at86_buffer[AT86RF215_MAX_PDU];

// TODO(chip setup): this handle is never initialised. Before the task touches the radio it needs, in order:
//   1. spi_m_sync_enable(&SPI_UHF) (currently only done inside at86_check_health)
//   2. at86_init_pvdx(&at86_dev)
//   3. at86rf215_radio_conf(&at86_dev, AT86RF215_RF09, ...) for carrier frequency/channel, set while in TRXOFF
//   4. TX power (at86rf215_set_pac), also in TRXOFF so the PA calibrates against it
//   5. change_at86_config(...) to pick the PHY
//   6. set_at86_rx_mode(&at86_dev, ...) so the radio starts out listening
static struct at86rf215 at86_dev;

// local uplink/downlink_queue item
radio_queue_item_t item;

void uhf_rx_process() {}

bool uhf_tx_process() {
    if (xQueueReceive(downlink_queue, &item, 0) == pdPASS) {
        size_t frame_sz = AT86RF215_MAX_PDU;
        if (uslp_mapp_request(at86_buffer, &frame_sz, item.data, item.len, RADIO_GMAP_ID(item.vcid), SPP_VERSION_NUMBER, RADIO_SDU_ID,
                              USLP_QOS_EXPEDITED)) {
            warning("USLP: failed to prepare frame\n");
        }

        int ret = at86rf215_tx_frame_irq(&at86_dev, AT86RF215_RF09, at86_buffer, frame_sz);
        if (ret) {
            warning("Radio: failed to send frame (%d)\n", ret);
        }
    }

    if (uxQueueMessagesWaiting(downlink_queue) == 0) {
        return false;
    }
    return true;
}

void uhf_go_rx() {
    size_t deadline = at86rf215_get_time_ms(h) + timeout_ms;
    int ret = supports_rf(h, radio);
    if (ret) {
        return ret;
    }

    /*
     * In order to transmit the frame, the corresponding baseband core should
     * be enabled
     */
    if (h->priv.chpm == AT86RF215_RF_MODE_RF) {
        return -AT86RF215_INVAL_CHPM;
    }
    if (radio == AT86RF215_RF09) {
        if (h->priv.chpm == AT86RF215_RF_MODE_BBRF09) {
            return -AT86RF215_INVAL_CHPM;
        }
    } else {
        if (h->priv.chpm == AT86RF215_RF_MODE_BBRF24) {
            return -AT86RF215_INVAL_CHPM;
        }
    }

    at86rf215_rf_state_t state;
    ret = at86rf215_get_state(h, &state, radio);
    if (ret) {
        return ret;
    }

    while (state != AT86RF215_STATE_RF_TRXOFF && state != AT86RF215_STATE_RF_TXPREP) {
        at86rf215_delay_us(h, 100);
        if (at86rf215_get_time_ms(h) > deadline) {
            return -AT86RF215_TIMEOUT;
        }

        /*
         * If the radio was in the TRXOFF state request again the TXPREP state
         */
        if (state == AT86RF215_STATE_RF_TRXOFF) {
            at86rf215_set_cmd(h, AT86RF215_CMD_RF_TXPREP, radio);
            at86rf215_delay_us(h, 100);
        }
        at86rf215_get_state(h, &state, radio);
    }

    at86rf215_delay_us(h, 100);

    /* Go to RX state*/
    while (state != AT86RF215_STATE_RF_RX) {
        if (at86rf215_get_time_ms(h) > deadline) {
            return -AT86RF215_TIMEOUT;
        }
        at86rf215_set_cmd(h, AT86RF215_CMD_RF_RX, radio);
        at86rf215_delay_us(h, 100);
        at86rf215_get_state(h, &state, radio);
    }
    return AT86RF215_OK;
}

void uhf_go_txprep() {}

int at86_check_health(struct at86rf215 *h, at86_health_t *health) {
    if (h == NULL || health == NULL) {
        return -AT86RF215_INVAL_PARAM;
    }

    // TODO: move to chip setup; nothing else enables SPI_UHF yet
    spi_m_sync_enable(&SPI_UHF);

    int ret = at86rf215_reg_read_8(h, &health->part_number, REG_RF_PN);
    if (ret) {
        return ret;
    }
    ret = at86rf215_reg_read_8(h, &health->version, REG_RF_VN);
    if (ret) {
        return ret;
    }
    ret = at86rf215_reg_read_8(h, &health->rf09_state, REG_RF09_STATE);
    if (ret) {
        return ret;
    }

    if (health->part_number != AT86RF215) {
        return -AT86RF215_UNKNOWN_IC;
    }

    return AT86RF215_OK;
}

/**
 * Issues one state command to RF09 and polls until the radio reaches the target state.
 *
 * The command is only accepted from the states in `from` (see the datasheet's state machine); from any other state
 * this returns -AT86RF215_INVAL_CONF without touching the radio.
 */
static int at86_change_state(struct at86rf215 *h, at86rf215_rf_cmd_t cmd, at86rf215_rf_state_t target, const at86rf215_rf_state_t *from,
                             size_t from_len, size_t timeout_ms) {
    at86rf215_radio_t radio = AT86RF215_RF09;

    if (h == NULL) {
        return -AT86RF215_INVAL_PARAM;
    }

    // The baseband core is bypassed in these modes, so frames can't be sent or received through it
    if (h->priv.chpm == AT86RF215_RF_MODE_RF || h->priv.chpm == AT86RF215_RF_MODE_BBRF09) {
        return -AT86RF215_INVAL_CHPM;
    }

    at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;
    int ret = at86rf215_get_state(h, &state, radio);
    if (ret) {
        return ret;
    }

    if (state == target) {
        return AT86RF215_OK;
    }

    bool allowed = false;
    for (size_t i = 0; i < from_len; i++) {
        if (state == from[i]) {
            allowed = true;
            break;
        }
    }
    if (!allowed) {
        return -AT86RF215_INVAL_CONF;
    }

    ret = at86rf215_set_cmd(h, cmd, radio);
    if (ret) {
        return ret;
    }

    // Compare elapsed time rather than a deadline, since the millisecond clock can wrap
    size_t start = at86rf215_get_time_ms(h);
    while (state != target) {
        if (at86rf215_get_time_ms(h) - start > timeout_ms) {
            return -AT86RF215_TIMEOUT;
        }
        at86rf215_delay_us(h, 100);
        ret = at86rf215_get_state(h, &state, radio);
        if (ret) {
            return ret;
        }
    }

    return AT86RF215_OK;
}

int set_at86_tx_prep_mode(struct at86rf215 *h, size_t timeout_ms) {
    // TXPREP is accepted from any non-sleep state; from TX or RX it aborts the frame in progress
    // Reference: AT86RF215 datasheet 5.2.7 (pg. 35)
    static const at86rf215_rf_state_t from[] = {AT86RF215_STATE_RF_TRXOFF, AT86RF215_STATE_RF_TX, AT86RF215_STATE_RF_RX,
                                                AT86RF215_STATE_RF_TRANSITION};
    return at86_change_state(h, AT86RF215_CMD_RF_TXPREP, AT86RF215_STATE_RF_TXPREP, from, sizeof(from) / sizeof(from[0]), timeout_ms);
}

int set_at86_rx_mode(struct at86rf215 *h, size_t timeout_ms) {
    // Reference: AT86RF215 datasheet 5.2.9 (pg. 36)
    static const at86rf215_rf_state_t from[] = {AT86RF215_STATE_RF_TRXOFF, AT86RF215_STATE_RF_TXPREP};
    return at86_change_state(h, AT86RF215_CMD_RF_RX, AT86RF215_STATE_RF_RX, from, sizeof(from) / sizeof(from[0]), timeout_ms);
}

#define AT86_BBC_IRQ_TXFE (1u << 4) // BBCn_IRQS/IRQM bit 4: transmitter frame end
#define AT86_FCS_LEN 2              // PC.FCST = AT86RF215_FCS_16 in every config

/**
 * Writes the PSDU to the BBC0 TX frame buffer as one SPI block write. The two COMMAND bytes and the data must be sent
 * within a single SELN-low sequence (datasheet 4.2.2/4.2.4), so this can't use at86rf215_spi_write(), which toggles
 * SELN on every call.
 */
static int at86_write_tx_buffer(struct at86rf215 *h, const uint8_t *buf, size_t sz) {
    static const uint8_t fcs_pad[AT86_FCS_LEN] = {0};
    uint8_t cmd[2] = {(REG_BBC0_FBTXS >> 8) | 0x80, REG_BBC0_FBTXS & 0xFF};
    struct spi_xfer xfer = {.txbuf = cmd, .rxbuf = NULL, .size = sizeof(cmd)};

    at86rf215_irq_enable(h, 0);
    at86rf215_set_seln(h, 0);
    int32_t sent = spi_m_sync_transfer(&SPI_UHF, &xfer);
    if (sent == (int32_t)xfer.size) {
        xfer.txbuf = (uint8_t *)buf;
        xfer.size = sz;
        sent = spi_m_sync_transfer(&SPI_UHF, &xfer);
    }
    if (sent == (int32_t)xfer.size) {
        xfer.txbuf = (uint8_t *)fcs_pad; // Add padding for fcs, gets replaced by chip on send
        xfer.size = sizeof(fcs_pad);
        sent = spi_m_sync_transfer(&SPI_UHF, &xfer);
    }
    at86rf215_set_seln(h, 1);
    at86rf215_irq_enable(h, 1);

    return sent == (int32_t)xfer.size ? AT86RF215_OK : -AT86RF215_INVAL_PARAM;
}

/**
 * Transmits one frame on RF09/BBC0. The radio must already be in TXPREP.
 *
 * Completion is detected by polling BBC0_IRQS.TXFE rather than the IRQ pin (which isn't wired up). TXFE only appears in
 * IRQS if it is unmasked in BBC0_IRQM or RF_CFG.IRQMM = 1; at86rf215_init() unmasks it.
 */
int at86_send_frame(struct at86rf215 *h, const uint8_t *buf, size_t sz, size_t timeout_ms) {
    at86rf215_radio_t radio = AT86RF215_RF09;

    if (h == NULL) {
        return -AT86RF215_INVAL_PARAM;
    }

    if (sz + AT86_FCS_LEN > AT86RF215_MAX_PDU) {
        return -AT86RF215_INVAL_PARAM;
    }

    if (!buf || sz == 0) {
        return -AT86RF215_INVAL_PARAM;
    }

    if (h->priv.chpm == AT86RF215_RF_MODE_RF || h->priv.chpm == AT86RF215_RF_MODE_BBRF09) {
        return -AT86RF215_INVAL_CHPM;
    }

    at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;
    int ret = at86rf215_get_state(h, &state, radio);
    if (ret) {
        return ret;
    }
    if (state != AT86RF215_STATE_RF_TXPREP) {
        return -AT86RF215_INVAL_CONF;
    }

    size_t psdu_len = sz + AT86_FCS_LEN; // PSDU len includes FCS
    ret = at86rf215_reg_write_8(h, psdu_len & 0xFF, REG_BBC0_TXFLL);
    if (ret) {
        return ret;
    }
    ret = at86rf215_reg_write_8(h, (psdu_len >> 8) & 0xFF, REG_BBC0_TXFLH);
    if (ret) {
        return ret;
    }

    ret = at86_write_tx_buffer(h, buf, sz);
    if (ret) {
        return ret;
    }

    // IRQS is cleared on read: discard any stale TXFE so it can't end the wait below early
    uint8_t irqs = 0;
    ret = at86rf215_reg_read_8(h, &irqs, REG_BBC0_IRQS);
    if (ret) {
        return ret;
    }

    // Data are in the buffer. Issue the TX cmd to send them
    ret = at86rf215_set_cmd(h, AT86RF215_CMD_RF_TX, radio);
    if (ret) {
        return ret;
    }

    // TXFE is set once the frame (incl. FCS and PA ramp-down) is sent; the radio then returns to TXPREP by itself
    size_t start = at86rf215_get_time_ms(h);
    irqs = 0;
    while (!(irqs & AT86_BBC_IRQ_TXFE)) {
        if (at86rf215_get_time_ms(h) - start > timeout_ms) {
            // Writing TXPREP while in TX aborts the transmission, leaving the radio ready for the next frame
            at86rf215_set_cmd(h, AT86RF215_CMD_RF_TXPREP, radio);
            return -AT86RF215_TIMEOUT;
        }
        at86rf215_delay_us(h, 100);
        ret = at86rf215_reg_read_8(h, &irqs, REG_BBC0_IRQS);
        if (ret) {
            return ret;
        }
    }

    return AT86RF215_OK;
}

// TODO: receive is not implemented yet. These stubs report that no frame is ever waiting, so the uplink path in
// main_uhf_radio never runs
int at86_peek_frame(struct at86rf215 *radio) {
    int ret = ready(radio);
    if (ret) {
        return ret;
    }

    return false;
}

int at86_check_irqs(void) {
    uint8_t irqs[4] = {0x0, 0x0, 0x0, 0x0};
}

/**
 * Wrapper for at86rf215_rx_frame
 */
int at86_pop_frame(struct at86rf215 *h, uint8_t *buf, size_t sz) {
    return at86rf215_rx_frame(h, buf, sz);
}

/**
 * PVDX-specific settings for PVDX
 */
int at86_init_pvdx(struct at86rf215 *radio) {
    radio->clko_os = AT86RF215_RF_CLKO_OFF;
    radio->clk_drv = AT86RF215_RF_DRVCLKO2;
    radio->rf_femode_09 = AT86RF215_RF_FEMODE1;
    radio->rf_femode_24 = AT86RF215_RF_FEMODE0;
    radio->xo_fs = 0;
    radio->xo_trim = 0;
    radio->irqmm = 0;
    radio->irqp = 0;
    radio->pad_drv = AT86RF215_RF_DRV2;
    int ret = at86rf215_init(radio);
    if (ret) {
        warning("AT86: init failed (%d)\n", ret);
    }
    return ret;
}

int change_at86_config(struct at86rf215 *h, at86_config_t config) {
    if (h == NULL) {
        return -AT86RF215_INVAL_PARAM;
    }

    struct at86rf215_bb_conf bb_conf;
    memset(&bb_conf, 0, sizeof(bb_conf));

    switch (config) {
        case AT86_O_QPSK_LEG:
            bb_conf.pt = AT86RF215_BB_MROQPSK;
            bb_conf.fcst = AT86RF215_FCS_16;
            bb_conf.txafcs = 1;
            bb_conf.qpsk.fchip = AT86RF215_MROQPSK_FCHIP_2000;
            bb_conf.qpsk.fshape_mod = AT86RF215_MROQPSK_BB_RRC08;
            bb_conf.qpsk.drate_mod = AT86RF215_MROQPSK_DRATE_MOD_0;
            bb_conf.qpsk.leg = 1;
            bb_conf.qpsk.fcstleg = 1;
            bb_conf.qpsk.rxm = AT86RF215_MROQPSK_RXM_LEGACY_ONLY;
            bb_conf.qpsk.nsfd = AT86RF215_MROQPSK_NSFD_SFD1;
            break;
        case AT86_MR_O_QPSK_100CH_6_2_5KB:
            bb_conf.pt = AT86RF215_BB_MROQPSK;
            bb_conf.fcst = AT86RF215_FCS_16;
            bb_conf.txafcs = 1;
            bb_conf.qpsk.fchip = AT86RF215_MROQPSK_FCHIP_100;
            bb_conf.qpsk.fshape_mod = AT86RF215_MROQPSK_BB_RRC08;
            bb_conf.qpsk.drate_mod = AT86RF215_MROQPSK_DRATE_MOD_0;
            bb_conf.qpsk.leg = 0;
            bb_conf.qpsk.fcstleg = 0;
            bb_conf.qpsk.rxm = AT86RF215_MROQPSK_RXM_MROQPSK_ONLY;
            bb_conf.qpsk.nsfd = AT86RF215_MROQPSK_NSFD_SFD1;
            break;
        // TODO: identical to the 6.25 kb/s config; 12.5 kb/s at 100 kchip/s is rate mode 1, i.e.
        // AT86RF215_MROQPSK_DRATE_MOD_1 (datasheet Table 6-100)
        case AT86_MR_O_QPSK_100CH_12_5KB:
            bb_conf.pt = AT86RF215_BB_MROQPSK;
            bb_conf.fcst = AT86RF215_FCS_16;
            bb_conf.txafcs = 1;
            bb_conf.qpsk.fchip = AT86RF215_MROQPSK_FCHIP_100;
            bb_conf.qpsk.fshape_mod = AT86RF215_MROQPSK_BB_RRC08;
            bb_conf.qpsk.drate_mod = AT86RF215_MROQPSK_DRATE_MOD_0;
            bb_conf.qpsk.leg = 0;
            bb_conf.qpsk.fcstleg = 0;
            bb_conf.qpsk.rxm = AT86RF215_MROQPSK_RXM_MROQPSK_ONLY;
            bb_conf.qpsk.nsfd = AT86RF215_MROQPSK_NSFD_SFD1;
            break;
        case AT86_MR_FSK:
            bb_conf.pt = AT86RF215_BB_MRFSK;
            bb_conf.fcst = AT86RF215_FCS_16;
            bb_conf.txafcs = 1;
            bb_conf.fsk.mord = AT86RF215_2FSK;
            bb_conf.fsk.midx = AT86RF215_MIDX_1;
            bb_conf.fsk.midxs = AT86RF215_MIDXS_88;
            bb_conf.fsk.bt = AT86RF215_FSK_BT_05;
            bb_conf.fsk.srate = AT86RF215_FSK_SRATE_50;
            bb_conf.fsk.preamble_length = 8;
            bb_conf.fsk.pdtm = 1;
            bb_conf.fsk.fecs = AT86RF215_FSK_FEC_RSC;
            bb_conf.fsk.fecie = 0;
            bb_conf.fsk.rxo = AT86RF215_FSK_RXO_DISABLED;
            bb_conf.fsk.sfd32 = 0; // Two 16-bit SFDs, sfd0 (uncoded) and sfd1 (coded)
            bb_conf.fsk.sfdq = 1;
            bb_conf.fsk.sfd = 1;
            bb_conf.fsk.csfd0 = AT86RF215_SFD_UNCODED_IEEE;
            bb_conf.fsk.csfd1 = AT86RF215_SFD_CODED_IEEE;
            bb_conf.fsk.sfd0 = 0x7209;
            bb_conf.fsk.sfd1 = 0x72F6;
            bb_conf.fsk.sfd_threshold = 8;
            bb_conf.fsk.preamble_threshold = 5;
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }

    int ret = at86rf215_bb_conf(h, AT86RF215_RF09, &bb_conf);
    if (ret) {
        warning("AT86: baseband config %d failed (%d)\n", config, ret);
    }
    return ret;
}
