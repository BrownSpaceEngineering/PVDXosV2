#include "at86_pvdx.h"

#include <string.h>

#include "atmel_start.h"
#include "logging.h"
#include "regs.h"

int
at86_init_pvdx(struct at86rf215 *radio)
{
  radio->clko_os      = AT86RF215_RF_CLKO_OFF;
  radio->clk_drv      = AT86RF215_RF_DRVCLKO2;
  radio->rf_femode_09 = AT86RF215_RF_FEMODE0;
  radio->rf_femode_24 = AT86RF215_RF_FEMODE0;
  radio->xo_fs        = 0;
  radio->xo_trim      = 0;
  radio->irqmm        = 0;
  radio->irqp         = 0;
  radio->pad_drv      = AT86RF215_RF_DRV2;
  int ret             = at86rf215_init(radio);
  if (ret) {
    warning("AT86: init failed (%d)\n", ret);
    return ret;
  }

  uint8_t auxs = 0;
  ret          = at86rf215_reg_read_8(radio, &auxs, REG_RF09_AUXS);
  if (!ret && (auxs & AT86_AUXS_EXTLNABYP)) {
    ret = at86rf215_reg_write_8(radio, auxs & ~AT86_AUXS_EXTLNABYP,
                                REG_RF09_AUXS);
  }
  if (ret) {
    warning("AT86: AUXS setup failed (%d)\n", ret);
    return ret;
  }

  ret = at86rf215_set_bbc_irq_mask(radio, AT86RF215_RF09,
                                   AT86_BBC_IRQ_RXFE | AT86_BBC_IRQ_TXFE);
  if (!ret) {
    ret = at86rf215_set_radio_irq_mask(radio, AT86RF215_RF09,
                                       AT86_RF_IRQ_WAKEUP | AT86_RF_IRQ_TRXRDY |
                                           AT86_RF_IRQ_TRXERR);
  }
  if (!ret) {
    ret = at86rf215_set_bbc_irq_mask(radio, AT86RF215_RF24, 0);
  }
  if (!ret) {
    ret = at86rf215_set_radio_irq_mask(radio, AT86RF215_RF24, 0);
  }
  if (ret) {
    warning("AT86: IRQ mask setup failed (%d)\n", ret);
  }
  return ret;
}

int
change_at86_config(struct at86rf215 *h, at86_config_t config)
{
  if (h == NULL) {
    return -AT86RF215_INVAL_PARAM;
  }

  struct at86rf215_bb_conf bb_conf;
  memset(&bb_conf, 0, sizeof(bb_conf));

  switch (config) {
  case AT86_O_QPSK_LEG:
    bb_conf.pt              = AT86RF215_BB_MROQPSK;
    bb_conf.fcst            = AT86RF215_FCS_16;
    bb_conf.txafcs          = 1;
    bb_conf.qpsk.fchip      = AT86RF215_MROQPSK_FCHIP_2000;
    bb_conf.qpsk.fshape_mod = AT86RF215_MROQPSK_BB_RRC08;
    bb_conf.qpsk.drate_mod  = AT86RF215_MROQPSK_DRATE_MOD_0;
    bb_conf.qpsk.leg        = 1;
    bb_conf.qpsk.fcstleg    = 1;
    bb_conf.qpsk.rxm        = AT86RF215_MROQPSK_RXM_LEGACY_ONLY;
    bb_conf.qpsk.nsfd       = AT86RF215_MROQPSK_NSFD_SFD1;
    break;
  case AT86_MR_O_QPSK_100CH_6_2_5KB:
    bb_conf.pt              = AT86RF215_BB_MROQPSK;
    bb_conf.fcst            = AT86RF215_FCS_16;
    bb_conf.txafcs          = 1;
    bb_conf.qpsk.fchip      = AT86RF215_MROQPSK_FCHIP_100;
    bb_conf.qpsk.fshape_mod = AT86RF215_MROQPSK_BB_RRC08;
    bb_conf.qpsk.drate_mod  = AT86RF215_MROQPSK_DRATE_MOD_0;
    bb_conf.qpsk.leg        = 0;
    bb_conf.qpsk.fcstleg    = 0;
    bb_conf.qpsk.rxm        = AT86RF215_MROQPSK_RXM_MROQPSK_ONLY;
    bb_conf.qpsk.nsfd       = AT86RF215_MROQPSK_NSFD_SFD1;
    break;
  case AT86_MR_O_QPSK_100CH_12_5KB:
    bb_conf.pt              = AT86RF215_BB_MROQPSK;
    bb_conf.fcst            = AT86RF215_FCS_16;
    bb_conf.txafcs          = 1;
    bb_conf.qpsk.fchip      = AT86RF215_MROQPSK_FCHIP_100;
    bb_conf.qpsk.fshape_mod = AT86RF215_MROQPSK_BB_RRC08;
    bb_conf.qpsk.drate_mod  = AT86RF215_MROQPSK_DRATE_MOD_0;
    bb_conf.qpsk.leg        = 0;
    bb_conf.qpsk.fcstleg    = 0;
    bb_conf.qpsk.rxm        = AT86RF215_MROQPSK_RXM_MROQPSK_ONLY;
    bb_conf.qpsk.nsfd       = AT86RF215_MROQPSK_NSFD_SFD1;
    break;
  case AT86_MR_FSK:
    bb_conf.pt                     = AT86RF215_BB_MRFSK;
    bb_conf.fcst                   = AT86RF215_FCS_16;
    bb_conf.txafcs                 = 1;
    bb_conf.fsk.mord               = AT86RF215_2FSK;
    bb_conf.fsk.midx               = AT86RF215_MIDX_1;
    bb_conf.fsk.midxs              = AT86RF215_MIDXS_88;
    bb_conf.fsk.bt                 = AT86RF215_FSK_BT_05;
    bb_conf.fsk.srate              = AT86RF215_FSK_SRATE_50;
    bb_conf.fsk.preamble_length    = 8;
    bb_conf.fsk.pdtm               = 1;
    bb_conf.fsk.fecs               = AT86RF215_FSK_FEC_RSC;
    bb_conf.fsk.fecie              = 0;
    bb_conf.fsk.rxo                = AT86RF215_FSK_RXO_DISABLED;
    bb_conf.fsk.sfd32              = 0;
    bb_conf.fsk.sfdq               = 1;
    bb_conf.fsk.sfd                = 1;
    bb_conf.fsk.csfd0              = AT86RF215_SFD_UNCODED_IEEE;
    bb_conf.fsk.csfd1              = AT86RF215_SFD_CODED_IEEE;
    bb_conf.fsk.sfd0               = 0x7209;
    bb_conf.fsk.sfd1               = 0x72F6;
    bb_conf.fsk.sfd_threshold      = 8;
    bb_conf.fsk.preamble_threshold = 5;
    break;
  case AT86_MR_FSK_LINK:
    bb_conf.pt                     = AT86RF215_BB_MRFSK;
    bb_conf.fcst                   = AT86RF215_FCS_16;
    bb_conf.txafcs                 = 1;
    bb_conf.fcsfe                  = 0;
    bb_conf.fsk.mord               = AT86RF215_2FSK;
    bb_conf.fsk.midx               = AT86RF215_MIDX_3;
    bb_conf.fsk.midxs              = AT86RF215_MIDXS_88;
    bb_conf.fsk.bt                 = AT86RF215_FSK_BT_10;
    bb_conf.fsk.srate              = AT86RF215_FSK_SRATE_50;
    bb_conf.fsk.fi                 = 0;
    bb_conf.fsk.preamble_length    = 8;
    bb_conf.fsk.rxo                = AT86RF215_FSK_RXO_18DB;
    bb_conf.fsk.fecie              = 1;
    bb_conf.fsk.fecs               = AT86RF215_FSK_FEC_NRNSC;
    bb_conf.fsk.sfd_threshold      = 8;
    bb_conf.fsk.preamble_threshold = 5;
    bb_conf.fsk.rawrbit            = 1;
    bb_conf.fsk.csfd0              = AT86RF215_SFD_UNCODED_IEEE;
    bb_conf.fsk.csfd1              = AT86RF215_SFD_CODED_IEEE;
    bb_conf.fsk.sfd0               = 0x7209;
    bb_conf.fsk.sfd1               = 0x72F6;
    bb_conf.fsk.sfd                = 0;
    bb_conf.fsk.dw                 = 0;
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

int
at86_check_health(struct at86rf215 *h, at86_health_t *health)
{
  if (h == NULL || health == NULL) {
    return -AT86RF215_INVAL_PARAM;
  }

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

at86_irqs_t
at86_read_irqs(struct at86rf215 *h)
{
  at86_irqs_t irqs = {0};
  if (at86rf215_reg_read_8(h, &irqs.rf, REG_RF09_IRQS) ||
      at86rf215_reg_read_8(h, &irqs.bbc, REG_BBC0_IRQS)) {
    warning("AT86: failed to read IRQ registers\n");
    irqs = (at86_irqs_t){0};
  }
  return irqs;
}

int
at86_go_txprep(struct at86rf215 *h)
{
  int ret = at86rf215_set_cmd(h, AT86RF215_CMD_RF_TXPREP, AT86RF215_RF09);
  if (ret) {
    warning("AT86: failed to request TXPREP (%d)\n", ret);
  }
  return ret;
}

int
at86_go_rx(struct at86rf215 *h)
{
  at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;
  int                  ret   = at86rf215_get_state(h, &state, AT86RF215_RF09);
  if (ret) {
    return ret;
  }
  if (state != AT86RF215_STATE_RF_TXPREP &&
      state != AT86RF215_STATE_RF_TRXOFF) {
    return -AT86RF215_INVAL_CONF;
  }

  ret = at86rf215_reg_write_8(h, AT86_PADFE_RX, REG_RF09_PADFE);
  if (ret) {
    return ret;
  }

  ret = at86rf215_set_cmd(h, AT86RF215_CMD_RF_RX, AT86RF215_RF09);
  if (ret) {
    warning("AT86: failed to request RX (%d)\n", ret);
    return ret;
  }

  ret = at86rf215_get_state(h, &state, AT86RF215_RF09);
  if (ret || (state != AT86RF215_STATE_RF_RX &&
              state != AT86RF215_STATE_RF_TRANSITION)) {
    warning("AT86: RX not reached (ret %d, state %d)\n", ret, state);
    return ret ? ret : -AT86RF215_INVAL_CONF;
  }
  return AT86RF215_OK;
}

static int
at86_write_tx_buffer(struct at86rf215 *h, const uint8_t *buf, size_t sz)
{
  static const uint8_t fcs_pad[AT86_FCS_LEN] = {0};
  uint8_t cmd[2]       = {(REG_BBC0_FBTXS >> 8) | 0x80, REG_BBC0_FBTXS & 0xFF};
  struct spi_xfer xfer = {.txbuf = cmd, .rxbuf = NULL, .size = sizeof(cmd)};

  at86rf215_irq_enable(h, 0);
  at86rf215_set_seln(h, 0);
  int32_t sent = spi_m_sync_transfer(&SPI_UHF, &xfer);
  if (sent == (int32_t)xfer.size) {
    xfer.txbuf = (uint8_t *)buf;
    xfer.size  = sz;
    sent       = spi_m_sync_transfer(&SPI_UHF, &xfer);
  }
  if (sent == (int32_t)xfer.size) {
    xfer.txbuf = (uint8_t *)fcs_pad;
    xfer.size  = sizeof(fcs_pad);
    sent       = spi_m_sync_transfer(&SPI_UHF, &xfer);
  }
  at86rf215_set_seln(h, 1);
  at86rf215_irq_enable(h, 1);

  return sent == (int32_t)xfer.size ? AT86RF215_OK : -AT86RF215_INVAL_PARAM;
}

int
at86_send_frame(struct at86rf215 *h, const uint8_t *buf, size_t sz)
{
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
  if (h->priv.chpm == AT86RF215_RF_MODE_RF ||
      h->priv.chpm == AT86RF215_RF_MODE_BBRF09) {
    return -AT86RF215_INVAL_CHPM;
  }

  at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;
  int                  ret   = at86rf215_get_state(h, &state, radio);
  if (ret) {
    return ret;
  }
  if (state != AT86RF215_STATE_RF_TXPREP) {
    return -AT86RF215_INVAL_CONF;
  }

  size_t psdu_len = sz + AT86_FCS_LEN;
  ret             = at86rf215_reg_write_8(h, psdu_len & 0xFF, REG_BBC0_TXFLL);
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

  ret = at86rf215_reg_write_8(h, AT86_PADFE_TX, REG_RF09_PADFE);
  if (ret) {
    return ret;
  }

  return at86rf215_set_cmd(h, AT86RF215_CMD_RF_TX, radio);
}
