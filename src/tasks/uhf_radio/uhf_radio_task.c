#include "uhf_radio_task.h"

#include <string.h>

#include "at86rf215.h"
#include "ccsds.h"
#include "ccsds/radio_routing.h"
#include "ccsds/uslp.h"
#include "hal_gpio.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "pusp.h"
#include "regs.h"
#include "spp.h"
#include "task_list.h"
#include "tasks/cfdp/cfdp_task.h"
#include "telemetry/telemetry.h"

#define AT86_RF_IRQ_WAKEUP (1u << 0)
#define AT86_RF_IRQ_TRXRDY (1u << 1)
#define AT86_RF_IRQ_TRXERR (1u << 4)
#define AT86_BBC_IRQ_RXFS (1u << 0)
#define AT86_BBC_IRQ_RXFE (1u << 1)
#define AT86_BBC_IRQ_TXFE (1u << 4)

#define AT86_BBC_PC_FCSOK (1u << 5) // BBCn_PC bit 5: FCS of the last received frame is valid
#define AT86_FCS_LEN 2              // PC.FCST = AT86RF215_FCS_16 in every config

#define AT86_IRQ GPIO(GPIO_PORTA, 27) // temp
#define AT86_IRQ_LINE 11
#define AT86_IRQ_PINMUX PINMUX_PA27A_EIC_EXTINT11
#define AT86_IRQ_NVIC EIC_11_IRQn

uint8_t at86_buffer[AT86RF215_MAX_PDU];

// Set up by uhf_chip_setup()
static struct at86rf215 at86_dev;

// True once uhf_chip_setup() has succeeded. Until then uhf_transmit_pusp() doesn't touch the radio, which is also what
// lets the PUSP tests (run before the radio is set up) drive the state machine
static bool uhf_radio_ready;

// Cleared by a PUSP_NOTIF_DISABLE_TX from the ground, see dispatch_pusp_immediate_cmd()
static bool uhf_tx_enabled = true;

// local uplink/downlink_queue item
radio_queue_item_t item;

// A whole space packet, between the queue item and the USLP frame
static uint8_t spp_buffer[RADIO_DOWNLINK_MAX_SIZE];

// BBC0 IRQs other than TXFE that uhf_take_txfe() read (and so cleared) while polling. uhf_read_irqs() hands them on.
// DATASHEET(verify): reading an IRQS register clears all of its bits, not just the one being polled (5.3.1, 5.3.2.4)
static uint8_t pending_bbc_irqs;

static bool uhf_send_frame_blocking(const uint8_t *frame, size_t len, const char *what);

// Ground station callsign, checked against the SPP secondary header of every uplinked packet that has one
static const uint8_t ground_callsign[SPP_SECONDARY_HEADER_CALLSIGN_SIZE] = {'b', 's', 'e', 'g', 'n', 'd'};

/**
 * Sets up IRQs for the At86
 */
void at86_irq_setup(void) {
    // set pin function
    gpio_set_pin_direction(AT86_IRQ, GPIO_DIRECTION_IN);
    gpio_set_pin_pull_mode(AT86_IRQ, GPIO_PULL_OFF);
    gpio_set_pin_function(AT86_IRQ, AT86_IRQ_PINMUX);

    // Note to Alex: big c told me this is what I needed to do for config. Seems reasonable but idrk.
    hri_mclk_set_APBAMASK_EIC_bit(MCLK);
    hri_gclk_write_PCHCTRL_reg(GCLK, EIC_GCLK_ID, GCLK_PCHCTRL_GEN_GCLK0 | GCLK_PCHCTRL_CHEN);

    hri_eic_clear_CTRLA_ENABLE_bit(EIC);
    hri_eic_write_CONFIG_SENSE3_bf(EIC, 1, EIC_CONFIG_SENSE3_RISE_Val);
    hri_eic_set_ASYNCH_ASYNCH_bf(EIC, 1u << AT86_IRQ_LINE);

    hri_eic_clear_INTFLAG_reg(EIC, 1u << AT86_IRQ_LINE);
    hri_eic_set_INTEN_reg(EIC, 1u << AT86_IRQ_LINE);
    hri_eic_set_CTRLA_ENABLE_bit(EIC);

    NVIC_ClearPendingIRQ(AT86_IRQ_NVIC);
    NVIC_SetPriority(AT86_IRQ_NVIC, 5);
    NVIC_EnableIRQ(AT86_IRQ_NVIC);
}

/**
 * Wakes radio task that then reads IRQ registers with uhf_read_irqs().
 */
void uhf_radio_irq_handler(void) {
    BaseType_t woken = pdFALSE;
    xTaskNotifyFromISR(p_uhf_radio_task->handle, UHF_EVENT_IRQ, eSetBits, &woken);
    portYIELD_FROM_ISR(woken);
}

uhf_irqs_t uhf_read_irqs(void) {
    uhf_irqs_t irqs = {0};
    if (at86rf215_reg_read_8(&at86_dev, &irqs.rf, REG_RF09_IRQS) || at86rf215_reg_read_8(&at86_dev, &irqs.bbc, REG_BBC0_IRQS)) {
        warning("Radio: failed to read IRQ registers\n");
        irqs = (uhf_irqs_t){0};
    }
    irqs.bbc |= pending_bbc_irqs;
    pending_bbc_irqs = 0;
    return irqs;
}

uint32_t uhf_irqs_to_events(uhf_irqs_t irqs) {
    uint32_t events = 0;

    if (irqs.rf & AT86_RF_IRQ_WAKEUP) {
        debug("Radio: wake-up, now in TRXOFF\n");
    }
    if (irqs.rf & AT86_RF_IRQ_TRXERR) {
        warning("Radio: transceiver error\n");
    }
    if (irqs.rf & AT86_RF_IRQ_TRXRDY) {
        events |= UHF_EVENT_TXRDY;
    }
    if (irqs.bbc & AT86_BBC_IRQ_RXFE) {
        events |= UHF_EVENT_RXFE;
    }
    if (irqs.bbc & AT86_BBC_IRQ_TXFE) {
        events |= UHF_EVENT_TXFE;
    }

    return events;
}

// CFDP PDU from the last uhf_rx_poll(), valid until the next one. Separate from item, which uhf_tx_process() dequeues
// into, so a transmit can't overwrite a PDU a state handler hasn't forwarded yet
static radio_queue_item_t rx_item;

/**
 * Reads the frame that set RXFE from the BBC0 RX buffer, checks it and its space packet, and classifies the packet by
 * APID into out. Leaves out->kind UHF_RX_NONE if the frame is dropped (the reason is logged). Forwards nothing and
 * doesn't touch the radio state.
 */
static void uhf_rx_classify(uhf_rx_t *out) {
    uint8_t pc = 0;
    if (at86rf215_reg_read_8(&at86_dev, &pc, REG_BBC0_PC) || !(pc & AT86_BBC_PC_FCSOK)) {
        warning("Radio: dropping frame with bad FCS\n");
        return;
    }

    uint8_t len_lo = 0;
    uint8_t len_hi = 0;
    if (at86rf215_reg_read_8(&at86_dev, &len_lo, REG_BBC0_RXFLL) || at86rf215_reg_read_8(&at86_dev, &len_hi, REG_BBC0_RXFLH)) {
        warning("Radio: failed to read frame length\n");
        return;
    }

    size_t psdu_len = ((size_t)(len_hi & 0x07) << 8) | len_lo; // PSDU length includes the FCS
    if (psdu_len <= AT86_FCS_LEN || psdu_len > AT86RF215_MAX_PDU) {
        warning("Radio: dropping frame with a %u byte PSDU\n", psdu_len);
        return;
    }

    size_t sz = psdu_len - AT86_FCS_LEN;
    if (at86rf215_rx_frame(&at86_dev, AT86RF215_RF09, at86_buffer, sz)) {
        warning("Radio: failed to read frame from radio\n");
        return;
    }

    // Both views point into at86_buffer, so everything needed is copied out before returning
    uslp_transfer_frame_view_t tframe_view;
    if (uslp_transfer_frame_parse(&tframe_view, at86_buffer, (uint32_t)sz)) {
        warning("Radio: dropping malformed USLP frame\n");
        return;
    }

    const uint8_t vcid = tframe_view.primary_header.virtual_channel_id;
    if (!radio_route_accepts_uplink(RADIO_UHF, vcid)) {
        warning("Radio: dropping USLP frame on unexpected VC %u\n", vcid);
        return;
    }

    if (tframe_view.data_field_header.tfdz_construction_rules != USLP_TFDZ_NO_SEGMENT ||
        tframe_view.data_field_header.protocol_identifier != USLP_UPID_SPACE_PACKETS) {
        warning("Radio: dropping USLP frame that doesn't carry one whole space packet\n");
        return;
    }

    spp_packet_view_t packet_view;
    if (spp_packet_parse(&packet_view, tframe_view.datafield, tframe_view.datafield_len)) {
        warning("Radio: dropping malformed space packet\n");
        return;
    }

    if (packet_view.header.packet_type != SPP_PACKET_TYPE_REQUESTING) {
        warning("Radio: dropping space packet that isn't a telecommand\n");
        return;
    }

    // data_length also counts the secondary header, which spp_packet_parse has already checked fits in the packet
    size_t len = (size_t)packet_view.header.data_length + 1;
    if (packet_view.header.secondary_header_flag) {
        len -= SPP_SECONDARY_HEADER_SIZE;

        if (memcmp(packet_view.secondary_header.callsign, ground_callsign, sizeof(ground_callsign)) != 0) {
            warning("Radio: dropping space packet with non-ground station callsign\n");
            return;
        }

        // Only trust the time once the packet is known to be from the ground station
        command_t cmd = (command_t){.target = p_adcs_task,
                                    .data = {.timestamp = packet_view.secondary_header.timestamp},
                                    .data_type = CMD_DATA_TIMESTAMP,
                                    .operation = OPERATION_SET_UNIX_TIME,
                                    .result = SUCCESS};
        enqueue_command(&cmd);
    }

    if (len == 0 || len > sizeof(rx_item.data)) {
        warning("Radio: dropping space packet with a %u byte data field\n", len);
        return;
    }

    // PUSP packets only travel on the PUSP VC, and CFDP PDUs never do
    const bool on_pusp_vc = vcid == radio_routes[RADIO_STREAM_PUSP].vcid;
    const uint16_t apid = packet_view.header.application_process_id;
    switch (apid) {
        case CCSDS_APID_CFDP:
            if (on_pusp_vc) {
                warning("Radio: dropping CFDP PDU on the PUSP VC\n");
                return;
            }
            rx_item.len = (uint16_t)len;
            rx_item.apid = CCSDS_APID_CFDP;
            rx_item.vcid = vcid;
            memcpy(rx_item.data, packet_view.data, len);
            out->kind = UHF_RX_CFDP;
            out->item = &rx_item;
            return;
        case CCSDS_APID_PUSP:
            if (!on_pusp_vc) {
                warning("Radio: dropping PUSP packet on VC %u\n", vcid);
                return;
            }
            if (len < sizeof(pusp_packet_t)) {
                warning("Radio: dropping %u byte PUSP packet\n", len);
                return;
            }
            out->kind = UHF_RX_PUSP;
            out->dir = ((const uint8_t *)packet_view.data)[0];
            out->notifs = ((const uint8_t *)packet_view.data)[1];
            debug("Radio: received PUSP directive %u, notifications 0x%x\n", out->dir, out->notifs);
            return;
        default:
            warning("Radio: dropping space packet with unknown APID 0x%x\n", apid);
            return;
    }
}

bool dispatch_pusp_immediate_cmd(uint8_t notifs) {
    if (notifs & ~PUSP_NOTIF_KNOWN) {
        warning("Radio: ignoring unknown PUSP notification bits 0x%x\n", notifs & ~PUSP_NOTIF_KNOWN);
    }

    const bool disable = (notifs & PUSP_NOTIF_DISABLE_TX) != 0;
    const bool enable = (notifs & PUSP_NOTIF_ENABLE_TX) != 0;
    if (disable && enable) {
        warning("Radio: PUSP notification both disables and enables transmit, ignored\n");
        return false;
    }
    if (!disable && !enable) {
        return false;
    }
    if (uhf_tx_enabled == enable) {
        debug("Radio: transmit already %s\n", enable ? "enabled" : "disabled");
        return false;
    }

    uhf_tx_enabled = enable;
    info("Radio: transmit %s by the ground\n", enable ? "enabled" : "disabled");
    if (!enable) {
        // Nothing CFDP queues can go out now, so end its transaction as cancelled. uhf_step() then drops the link,
        // which the CFDP task may notice first: it drops the transaction anyway and discards this cancel
        cfdp_cancel();
    }
    return true;
}

// Set by uhf_request_transmit() from another task, applied by the UHF task
static volatile uint8_t tx_request_notifs;

void uhf_request_transmit(bool enable) {
    tx_request_notifs = enable ? PUSP_NOTIF_ENABLE_TX : PUSP_NOTIF_DISABLE_TX;
    if (p_uhf_radio_task->handle != NULL) {
        xTaskNotify(p_uhf_radio_task->handle, UHF_EVENT_TX_REQUEST, eSetBits);
    }
}

void uhf_apply_transmit_request(void) {
    info("Radio: transmit %s requested by uplink command\n", tx_request_notifs == PUSP_NOTIF_ENABLE_TX ? "on" : "off");
    dispatch_pusp_immediate_cmd(tx_request_notifs);
}

bool uhf_tx_is_enabled(void) {
    return uhf_tx_enabled;
}

uhf_rx_t uhf_rx_poll(uhf_ctx_t *ctx, uint32_t events) {
    uhf_rx_t rx = {.kind = UHF_RX_NONE};
    if (!(events & UHF_EVENT_IRQ)) {
        return rx;
    }
    if (!(uhf_irqs_to_events(uhf_read_irqs()) & UHF_EVENT_RXFE)) {
        return rx;
    }

    uhf_rx_classify(&rx);
    uhf_go_rx(); // RXFE leaves the radio in TXPREP, whether or not the frame was any good

    if (rx.kind != UHF_RX_NONE) {
        ctx->last_rx = xTaskGetTickCount();
    }
    return rx;
}

/**
 * Sends the next downlink packet, if there is one, and waits for it to go out (see uhf_send_frame_blocking()). Leaves
 * the radio in TXPREP. A packet that can't be wrapped or sent is dropped (logged); CFDP's own retransmission covers it.
 *
 * \return true if more packets are waiting
 */
bool uhf_tx_process() {
    if (xQueueReceive(downlink_queue, &item, 0) == pdPASS) {
        size_t packet_len = 0;
        size_t frame_sz = AT86RF215_MAX_PDU;
        if (spp_wrap(&item, spp_buffer, sizeof(spp_buffer), &packet_len)) {
            warning("SPP: failed to wrap %u byte packet for APID 0x%x\n", item.len, item.apid);
        } else if (uslp_mapp_request(at86_buffer, &frame_sz, spp_buffer, (uint16_t)packet_len, RADIO_GMAP_ID(item.vcid), SPP_VERSION_NUMBER,
                                     RADIO_SDU_ID, USLP_QOS_EXPEDITED)) {
            warning("USLP: failed to prepare frame\n");
        } else {
            uhf_send_frame_blocking(at86_buffer, frame_sz, "downlink frame");
        }
    }

    return uxQueueMessagesWaiting(downlink_queue) != 0;
}

/**
 * Sends cmd=RX and checks for either state transition to RX or TRANSITION
 * states.
 *
 * \return 0 on success or negative error code
 */
int uhf_go_rx() {
    int ret = at86rf215_set_cmd(&at86_dev, AT86RF215_CMD_RF_RX, AT86RF215_RF09);
    if (ret) {
        warning("Radio: failed to request RX (%d)\n", ret);
        return ret;
    }

    at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;
    ret = at86rf215_get_state(&at86_dev, &state, AT86RF215_RF09);
    if (ret || (state != AT86RF215_STATE_RF_RX && state != AT86RF215_STATE_RF_TRANSITION)) {
        warning("Radio: RX not reached (ret %d, state %d)\n", ret, state);
        return ret ? ret : -AT86RF215_INVAL_CONF;
    }
    return AT86RF215_OK;
}

/**
 * Sends cmd=TXPREP. Doesn't wait: callers poll the state (uhf_wait_txprep()).
 *
 * ERRATA(#2, not handled): from TRXOFF, e.g. after a WAKEUP, this transition can hang in TRANSITION on v.1 silicon.
 * The task normally only comes here from RX or TXPREP, which the erratum doesn't cover. See uhf_chip_setup()
 */
void uhf_go_txprep() {
    int ret = at86rf215_set_cmd(&at86_dev, AT86RF215_CMD_RF_TXPREP, AT86RF215_RF09);
    if (ret) {
        warning("Radio: failed to request TXPREP (%d)\n", ret);
    }
}

int at86_check_health(struct at86rf215 *h, at86_health_t *health) {
    if (h == NULL || health == NULL) {
        return -AT86RF215_INVAL_PARAM;
    }

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
 * Starts sending one frame on RF09/BBC0. Radio must already be in TXPREP. The TXFE IRQ reports the end of the
 * frame. radio then returns to TXPREP by itself.
 */
int at86_send_frame(struct at86rf215 *h, const uint8_t *buf, size_t sz) {
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

    // Data are in the buffer. Issue the TX cmd to send them
    return at86rf215_set_cmd(h, AT86RF215_CMD_RF_TX, radio);
}

/**
 * PVDX-specific settings for PVDX
 */
int at86_init_pvdx(struct at86rf215 *radio) {
    // TODO(hardware): which front-end mode RF09 needs depends on how the board's PA/LNA switch is wired. The burn sequence's
    // copy (at86_pvdx.c) uses FEMODE0, switches PADFE by hand between TX and RX, and clears AUXS.EXTLNABYP; this does none
    // of that. Decide on hardware
    // TODO(hardware): xo_fs/xo_trim are untrimmed, so the carrier may be off frequency until they're measured
    radio->clko_os = AT86RF215_RF_CLKO_OFF;
    radio->clk_drv = AT86RF215_RF_DRVCLKO2;
    radio->rf_femode_09 = AT86RF215_RF_FEMODE1;
    radio->rf_femode_24 = AT86RF215_RF_FEMODE0;
    radio->xo_fs = 0;
    radio->xo_trim = 0;
    // DATASHEET(verify): with RF_CFG.IRQMM = 1, masked IRQ reasons still appear in the IRQS registers; with 0 they don't
    // (4.3.1.1, 5.3.1, 5.3.2.5). Needed so TXFE can be polled while masked from the IRQ pin
    radio->irqmm = 1;
    radio->irqp = 0;
    radio->pad_drv = AT86RF215_RF_DRV2;
    int ret = at86rf215_init(radio);
    if (ret) {
        warning("AT86: init failed (%d)\n", ret);
        return ret;
    }

    // at86rf215_init() unmasks TXFE and TRXRDY on both radios. Unmask the RF09 IRQs that the task handles, and mask all
    // RF24/BBC1 IRQs: the task never reads their IRQS registers, so a pending one would hold the IRQ pin active.
    // DATASHEET(verify): TXFE and TRXRDY are masked from the pin, which relies on irqmm = 1 above. TXFE is polled by
    // uhf_take_txfe(), and state changes by reading RF09_STATE, so on the pin they would only cause spurious wakeups
    ret = at86rf215_set_bbc_irq_mask(radio, AT86RF215_RF09, AT86_BBC_IRQ_RXFS | AT86_BBC_IRQ_RXFE);
    if (!ret) {
        ret = at86rf215_set_radio_irq_mask(radio, AT86RF215_RF09, AT86_RF_IRQ_WAKEUP | AT86_RF_IRQ_TRXERR);
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

#define UHF_SETUP_TRY(call)                                                                                                                \
    do {                                                                                                                                   \
        int setup_ret = (call);                                                                                                            \
        if (setup_ret != AT86RF215_OK) {                                                                                                   \
            warning("Radio: %s failed (%d)\n", #call, setup_ret);                                                                          \
            return false;                                                                                                                  \
        }                                                                                                                                  \
    } while (0)

/**
 * Polls until the radio reaches TXPREP, which is where the PLL locks.
 *
 * \return true if it got there within timeout_ms
 */
static bool uhf_wait_txprep(uint32_t timeout_ms) {
    const TickType_t start = xTaskGetTickCount();
    do {
        at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;
        if (at86rf215_get_state(&at86_dev, &state, AT86RF215_RF09) == AT86RF215_OK && state == AT86RF215_STATE_RF_TXPREP) {
            return true;
        }
        vTaskDelay(1);
    } while (xTaskGetTickCount() - start < pdMS_TO_TICKS(timeout_ms));
    return false;
}

/**
 * Reads BBC0_IRQS, which clears it. Any bits other than TXFE are kept for uhf_read_irqs(), and the task is notified so
 * they get handled.
 *
 * \return true if TXFE was set: a frame finished sending since the last read
 */
static bool uhf_take_txfe(void) {
    uint8_t bbc = 0;
    if (at86rf215_reg_read_8(&at86_dev, &bbc, REG_BBC0_IRQS)) {
        return false;
    }
    const uint8_t other = bbc & (uint8_t)~AT86_BBC_IRQ_TXFE;
    if (other) {
        pending_bbc_irqs |= other;
        xTaskNotify(xTaskGetCurrentTaskHandle(), UHF_EVENT_IRQ, eSetBits);
    }
    return (bbc & AT86_BBC_IRQ_TXFE) != 0;
}

/**
 * Polls BBC0_IRQS.TXFE until the frame being sent is out.
 *
 * \return true if it went out within timeout_ms
 */
static bool uhf_wait_txfe(uint32_t timeout_ms) {
    const TickType_t start = xTaskGetTickCount();
    do {
        if (uhf_take_txfe()) {
            return true;
        }
        vTaskDelay(1);
    } while (xTaskGetTickCount() - start < pdMS_TO_TICKS(timeout_ms));
    return false;
}

bool uhf_chip_setup(void) {
    uhf_radio_ready = false;
    spi_m_sync_enable(&SPI_UHF);

    UHF_SETUP_TRY(at86_init_pvdx(&at86_dev));
    UHF_SETUP_TRY(at86rf215_conn_check(&at86_dev));
    UHF_SETUP_TRY(at86rf215_set_mode(&at86_dev, AT86RF215_RF_MODE_BBRF));

    const struct at86rf215_radio_conf radio_conf = {
        .cm = AT86RF215_CM_FINE_RES_04, // Covers 389.5-510 MHz, and is the mode at86rf215_set_freq needs
        .cs = 0,
        .base_freq = 0,
        .lbw = AT86RF215_PLL_LBW_DEFAULT,
    };
    UHF_SETUP_TRY(at86rf215_radio_conf(&at86_dev, AT86RF215_RF09, &radio_conf));

    UHF_SETUP_TRY(change_at86_config(&at86_dev, UHF_PHY_CONFIG));
    UHF_SETUP_TRY(at86rf215_bb_enable(&at86_dev, AT86RF215_RF09, 1));

    UHF_SETUP_TRY(at86rf215_set_freq(&at86_dev, AT86RF215_RF09, UHF_FREQ_HZ));
    UHF_SETUP_TRY(at86rf215_set_pac(&at86_dev, AT86RF215_RF09, AT86RF215_PACUR_NO_RED, UHF_TX_POWER));

    // ERRATA(#2, not handled): on v.1 silicon (RF_VN = 1) TRXOFF -> TXPREP can hang in TRANSITION. If it isn't
    // reached within 200 us the workaround is RF09_PLL = 9, wait 20 us, RF09_PLL = 8, repeated until TXPREP (datasheet
    // 15.2.2). Without it this times out, setup fails and main_uhf_radio() retries the whole setup UHF_SETUP_RETRY_MS later
    UHF_SETUP_TRY(at86rf215_set_cmd(&at86_dev, AT86RF215_CMD_RF_TXPREP, AT86RF215_RF09));
    if (!uhf_wait_txprep(UHF_STATE_TIMEOUT_MS)) {
        warning("Radio: did not reach TXPREP\n");
        return false;
    }

    at86rf215_pll_ls_t pll = AT86RF215_PLL_UNLOCKED;
    UHF_SETUP_TRY(at86rf215_get_pll_ls(&at86_dev, &pll, AT86RF215_RF09));
    if (pll != AT86RF215_PLL_LOCKED) {
        warning("Radio: PLL did not lock\n");
        return false;
    }

    // Clear the IRQs setup left pending: TRXRDY from reaching TXPREP (masked now, but still in IRQS), and anything
    // at86rf215_init() unmasked that fired before at86_init_pvdx() masked it again
    UHF_SETUP_TRY(at86rf215_irq_clear(&at86_dev));
    UHF_SETUP_TRY(uhf_go_rx());

    uhf_radio_ready = true;
    info("Radio: chip set up (%lu Hz, power %d), listening\n", (unsigned long)UHF_FREQ_HZ, UHF_TX_POWER);
    return true;
}

// With the asserts at the top of uhf_radio_main.c (a space packet fits in a USLP frame, a USLP frame fits in an AT86
// frame), one telemetry downlink is one SPP packet, one USLP frame and one AT86 frame
_Static_assert(TELEMETRY_DOWNLINK_SIZE <= RADIO_PAYLOAD_MAX_SIZE, "a telemetry downlink must fit in one space packet");

// The frame every beacon of a burst sends, built once per burst by uhf_beacon_prepare(). 0 length until then
static uint8_t beacon_frame[USLP_MAX_FRAME_SIZE];
static size_t beacon_frame_len;

bool uhf_beacon_prepare(void) {
    // Static: both are too big for the task's stack
    static tel_downlink_t downlink;
    static radio_queue_item_t beacon_item;

    beacon_frame_len = 0;

    telemetry_collect(&downlink);
    if (telemetry_downlink_serialize(&downlink, beacon_item.data, sizeof(beacon_item.data))) {
        warning("Radio: failed to serialize beacon telemetry\n");
        return false;
    }
    beacon_item.len = TELEMETRY_DOWNLINK_SIZE;
    beacon_item.apid = CCSDS_APID_TELEMETRY;
    beacon_item.vcid = radio_routes[RADIO_STREAM_TELEMETRY].vcid;

    size_t packet_len = 0;
    if (spp_wrap(&beacon_item, spp_buffer, sizeof(spp_buffer), &packet_len)) {
        warning("SPP: failed to wrap beacon\n");
        return false;
    }

    size_t frame_sz = sizeof(beacon_frame);
    if (uslp_mapp_request(beacon_frame, &frame_sz, spp_buffer, (uint16_t)packet_len, RADIO_GMAP_ID(beacon_item.vcid), SPP_VERSION_NUMBER,
                          RADIO_SDU_ID, USLP_QOS_EXPEDITED)) {
        warning("USLP: failed to prepare beacon frame\n");
        return false;
    }

    beacon_frame_len = frame_sz;
    return true;
}

/**
 * Sends one prepared frame: TXPREP, send, poll until the frame is out. Leaves the radio in TXPREP. Refuses while
 * transmit is disabled.
 *
 * \param what names the frame in warnings
 * \return true if the frame was sent within UHF_TX_TIMEOUT_MS (failures are logged)
 */
static bool uhf_send_frame_blocking(const uint8_t *frame, size_t len, const char *what) {
    // Every transmit comes through here. uhf_step() already keeps the task out of states that send while transmit is
    // disabled; this is the backstop
    if (!uhf_tx_enabled) {
        warning("Radio: transmit disabled, %s not sent\n", what);
        return false;
    }

    // DATASHEET(verify): assumes CMD=TXPREP while already in TXPREP (e.g. after the previous frame of a burst) is
    // harmless and the state reads TXPREP straight away. The state diagram (5.2) doesn't show this transition explicitly
    uhf_go_txprep();
    if (!uhf_wait_txprep(UHF_STATE_TIMEOUT_MS)) {
        warning("Radio: did not reach TXPREP for %s\n", what);
        return false;
    }

    // Clear a TXFE left over from an earlier frame, so it isn't taken for this one. DATASHEET(verify): assumes TXFE stays
    // set in IRQS until read, so a stale one is possible (5.3.2.4)
    uhf_take_txfe();

    int ret = at86_send_frame(&at86_dev, frame, len);
    if (ret) {
        warning("Radio: failed to send %s (%d)\n", what, ret);
        return false;
    }

    // DATASHEET(verify): in baseband mode TXFE is set when the frame is out and the radio goes from TX to TXPREP by
    // itself, without a TRXRDY IRQ (5.2.7, 5.3.2.4 IRQS.TXFE)
    if (!uhf_wait_txfe(UHF_TX_TIMEOUT_MS)) {
        warning("Radio: %s not sent within %d ms\n", what, UHF_TX_TIMEOUT_MS);
        return false;
    }
    return true;
}

bool uhf_transmit_beacon(void) {
    if (beacon_frame_len == 0) {
        warning("Radio: no beacon frame prepared\n");
        return false;
    }
    return uhf_send_frame_blocking(beacon_frame, beacon_frame_len, "beacon");
}

bool uhf_rx_is_pusp(const uhf_rx_t *rx, pusp_directive_t dir) {
    return rx->kind == UHF_RX_PUSP && rx->dir == dir;
}

void uhf_forward_cfdp(const uhf_rx_t *rx) {
    if (rx->kind != UHF_RX_CFDP || rx->item == NULL) {
        return;
    }
    if (uplink_queue == NULL) {
        warning("Radio: no uplink queue yet, dropping packet\n");
        return;
    }
    if (xQueueSend(uplink_queue, rx->item, 0) != pdPASS) {
        warning("Radio: uplink queue full, dropping packet\n");
        return;
    }
    radio_cfdp_input_forwarded();
}

pusp_directive_t uhf_pusp_tx_last;
uint32_t uhf_pusp_tx_count;

bool uhf_transmit_pusp(pusp_directive_t dir) {
    // Recorded before anything can fail, so the PUSP tests see every directive the state machine asked for
    uhf_pusp_tx_last = dir;
    uhf_pusp_tx_count++;

    if (!uhf_radio_ready) {
        debug("Radio: radio not set up, PUSP directive %u not sent\n", dir);
        return false;
    }

    // Static: a queue item is too big for the task's stack
    static radio_queue_item_t pusp_item;
    const pusp_packet_t packet = {.dir = dir};
    pusp_item.len = sizeof(packet);
    pusp_item.apid = CCSDS_APID_PUSP;
    pusp_item.vcid = radio_routes[RADIO_STREAM_PUSP].vcid;
    memcpy(pusp_item.data, &packet, sizeof(packet));

    // spp_buffer and at86_buffer are shared with the rest of the task. That's safe: everything runs on the radio task,
    // and uhf_rx_classify() has copied everything it needs out of at86_buffer before a state handler gets here
    bool sent = false;
    size_t packet_len = 0;
    size_t frame_sz = sizeof(at86_buffer);
    if (spp_wrap(&pusp_item, spp_buffer, sizeof(spp_buffer), &packet_len)) {
        warning("SPP: failed to wrap PUSP directive %u\n", dir);
    } else if (uslp_mapp_request(at86_buffer, &frame_sz, spp_buffer, (uint16_t)packet_len, RADIO_GMAP_ID(pusp_item.vcid),
                                 SPP_VERSION_NUMBER, RADIO_SDU_ID, USLP_QOS_EXPEDITED)) {
        warning("USLP: failed to prepare PUSP frame\n");
    } else {
        sent = uhf_send_frame_blocking(at86_buffer, frame_sz, "PUSP directive");
        if (sent) {
            debug("Radio: sent PUSP directive %u\n", dir);
        }
    }

    // Back to RX whether or not the frame went out, so the ground's reply (or its re-send) isn't missed
    uhf_go_rx();
    return sent;
}
