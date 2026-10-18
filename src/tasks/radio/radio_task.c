#include "radio_task.h"

#include "at86rf215.h"
#include "regs.h"

bool radio_transmit(const uint8_t *frame, size_t len) {
    return false;
}

int at86_check_health(at86_health_t *health) {
    at86rf215_radio_t radio = AT86RF215_RF09;

    if (health == NULL) {
        return -AT86RF215_INVAL_PARAM;
    }

    spi_m_sync_enable(&SPI_UHF);
    int ret = at86rf215_reg_read_8(&radio, &health->part_number, REG_RF_PN);
    ret = at86rf215_reg_read_8(&radio, &health->verison, REG_RF_VN);
    ret = at86rf215_reg_read_8(&radio, &health->rf09_state, REG_RF09_STATE);

    if (health->part_number != AT86RF215) {
        return -AT86RF215_UNKNOWN_IC;
    }

    return AT86RF215_OK;
}

int set_at86_tx_mode(struct at86rf215 *h, size_t timeout_ms) {
    at86rf215_radio_t radio = AT86RF215_RF09;

    size_t deadline = at86rf215_get_time_ms(h) + timeout_ms;

    if (h->priv.chpm == AT86RF215_RF_MODE_RF || h->priv.chpm == AT86RF215_RF_MODE_BBRF09) {
        return -AT86RF215_INVAL_CHPM;
    }

    at86rf215_rf_state_t current_state = AT86RF215_STATE_RF_TRANSITION;
    at86rf215_get_state(h, &current_state, radio);

    if (current_state == AT86RF215_STATE_RF_TXPREP) {
        return AT86RF215_OK;
    }

    if (current_state != AT86RF215_STATE_RF_TX || current_state != AT86RF215_STATE_RF_RX || current_state != AT86RF215_STATE_RF_TRXOFF) {
        return -AT86RF215_INVAL_CONF;
    }

    at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;
    while (state != AT86RF215_STATE_RF_TXPREP) {
        if (at86rf215_get_time_ms(h) > deadline) {
            return -AT86RF215_TIMEOUT;
        }
        at86rf215_set_cmd(h, AT86RF215_CMD_RF_TXPREP, radio);
        at86rf215_delay_us(h, 100);
        at86rf215_get_state(h, &state, radio);
    }

    return AT86RF215_OK;
}

int set_at86_rx_mode(struct at86rf215 *h, size_t timeout_ms) {
    at86rf215_radio_t radio = AT86RF215_RF09;

    size_t deadline = at86rf215_get_time_ms(h) + timeout_ms;

    if (h->priv.chpm == AT86RF215_RF_MODE_RF || h->priv.chpm == AT86RF215_RF_MODE_BBRF09) {
        return -AT86RF215_INVAL_CHPM;
    }

    at86rf215_rf_state_t current_state;
    at86rf215_get_state(h, &current_state, radio);

    if (current_state == AT86RF215_STATE_RF_RX) {
        return AT86RF215_OK;
    }

    if (current_state != AT86RF215_STATE_RF_TXPREP || current_state != AT86RF215_STATE_RF_TRXOFF) {
        return -AT86RF215_INVAL_CONF;
    }

    at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;
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

    size_t deadline = at86rf215_get_time_ms(h) + timeout_ms;

    if (sz > AT86_MAX_PSDU_SIZE) {
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
    irqs = 0;
    while (!(irqs & AT86_BBC_IRQ_TXFE)) {
        if (at86rf215_get_time_ms(h) > deadline) {
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
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "tx init successful\n");
    return ret;
}

int change_at86_config(struct at86rf215 *radio, at86_config_t *config) {
    struct at86rf215_bb_conf bb_conf;
    memset(&bb_conf, 0, sizeof(bb_conf));

    switch (at86_config_t) {
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
        case AT86_MR_O_QPSK_100CH_12_5_KB:
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
            return -1;
            break;
    }
    int ret = at86rf215_bb_conf(&radio, AT86RF215_RF09, &bb_conf);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "tx bb_conf\n");
    return ret;
}
