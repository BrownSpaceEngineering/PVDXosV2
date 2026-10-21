#include "uhf_radio_task.h"

#include <string.h>

#include "at86rf215.h"
#include "logging.h"
#include "regs.h"

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

/**
 * Writes the PSDU to the BBC0 TX frame buffer as one SPI block write. The two COMMAND bytes and the data must be sent
 * within a single SELN-low sequence (datasheet 4.2.2/4.2.4), so this can't use at86rf215_spi_write(), which toggles
 * SELN on every call.
 */
static int at86_write_tx_buffer(struct at86rf215 *h, const uint8_t *buf, size_t sz) {
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

    if (sz > AT86RF215_MAX_PDU) {
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

    /* Declare the size of the PSDU */
    ret = at86rf215_reg_write_8(h, sz & 0xFF, REG_BBC0_TXFLL);
    if (ret) {
        return ret;
    }
    ret = at86rf215_reg_write_8(h, (sz >> 8) & 0xFF, REG_BBC0_TXFLH);
    if (ret) {
        return ret;
    }

    ret = at86_write_tx_buffer(h, buf, sz);
    if (ret) {
        return ret;
    }

    /* IRQS is cleared on read: discard any stale TXFE so it can't end the wait below early */
    uint8_t irqs = 0;
    ret = at86rf215_reg_read_8(h, &irqs, REG_BBC0_IRQS);
    if (ret) {
        return ret;
    }

    /* Data are in the buffer. Issue the TX cmd to send them */
    ret = at86rf215_set_cmd(h, AT86RF215_CMD_RF_TX, radio);
    if (ret) {
        return ret;
    }

    /* TXFE is set once the frame (incl. FCS and PA ramp-down) is sent; the radio then returns to TXPREP by itself */
    size_t start = at86rf215_get_time_ms(h);
    irqs = 0;
    while (!(irqs & AT86_BBC_IRQ_TXFE)) {
        if (at86rf215_get_time_ms(h) - start > timeout_ms) {
            /* Writing TXPREP while in TX aborts the transmission, leaving the radio ready for the next frame */
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
bool at86_peek_frame(void) {
    return false;
}

int at86_pop_frame(uint8_t *buf, size_t sz) {
    return -AT86RF215_NOT_IMPL;
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
        // TODO: legacy O-QPSK has a 7-bit frame length (datasheet Figure 6-27), so frames are at most 127 bytes:
        // too small for one RADIO_DOWNLINK_MAX_SIZE packet, let alone a USLP_MAX_FRAME_SIZE frame. Either drop this
        // mode or cap the size passed to uslp_mapp_request while it is selected
        // TODO: 2000 kchip/s is the 2450 MHz legacy PHY; the 780/915 MHz one is 1000 kchip/s (datasheet Table 6-101).
        // Confirm 2000 is intended on RF09 and that its ~2 MHz bandwidth is allowed in our band
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
        default:
            return -AT86RF215_INVAL_PARAM;
    }

    int ret = at86rf215_bb_conf(h, AT86RF215_RF09, &bb_conf);
    if (ret) {
        warning("AT86: baseband config %d failed (%d)\n", config, ret);
    }
    return ret;
}
