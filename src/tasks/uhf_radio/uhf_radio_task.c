#include "uhf_radio_task.h"

#include <string.h>

#include "at86rf215.h"
#include "ccsds/radio_routing.h"
#include "ccsds/uslp.h"
#include "hal_gpio.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "pusp.h"
#include "regs.h"
#include "task_list.h"

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

// local uplink/downlink_queue item
radio_queue_item_t item;

// A whole space packet, between the queue item and the USLP frame
static uint8_t spp_buffer[RADIO_DOWNLINK_MAX_SIZE];

// Ground station callsign, checked against the SPP secondary header of every uplinked packet that has one
static const uint8_t ground_callsign[SPP_SECONDARY_HEADER_CALLSIGN_SIZE] = {'b', 's', 'e', 'g', 'n', 'd'};

/**
 * The next SPP sequence count for apid. CCSDS counts packets separately for each APID (SPP Blue Book 4.1.3.4.3)
 */
static uint16_t spp_next_seq_count(uint16_t apid) {
    static uint16_t cfdp_count = 0;
    static uint16_t pusp_count = 0;
    static uint16_t other_count = 0;

    uint16_t *count;
    switch (apid) {
        case CCSDS_APID_CFDP:
            count = &cfdp_count;
            break;
        case CCSDS_APID_PUSP:
            count = &pusp_count;
            break;
        default:
            count = &other_count;
            break;
    }
    uint16_t ret = *count & 0x3FFF;
    *count = (uint16_t)((*count + 1) & 0x3FFF);
    return ret;
}

/**
 * Wraps a queue item's payload in an SPP packet, written to buf
 *
 * \return true on failure, else false
 */
static bool spp_wrap(const radio_queue_item_t *it, uint8_t *buf, size_t buf_size, size_t *packet_len) {
    spp_primary_packet_header_t primary_header = {
        .version_number = SPP_VERSION_NUMBER,
        .packet_type = SPP_PACKET_TYPE_REPORTING,
        .secondary_header_flag = SPP_SECONDARY_HEADER_PRESENT,
        .application_process_id = it->apid,
        .sequence_flags = SPP_SEQ_FLAG_UNSEGMENTED_DATA,
        .sequence_count = spp_next_seq_count(it->apid),
    };

    // TODO(temp): placeholder timestamps and callsign. Transmitting on UHF needs the real callsign before flight
    spp_secondary_packet_header_t secondary_header = {
        .timestamp = 0,                             // TODO(temp): unix time
        .boot_timestamp = 0,                        // TODO(temp): unix time of last boot
        .callsign = {'B', 'S', 'E', 'B', 'S', 'E'}, // TODO(temp): real callsign
    };

    return spp_packet_serialize(&primary_header, &secondary_header, it->data, it->len, buf, buf_size, packet_len);
}

/**
 * Handles a PUSP packet received from the ground
 *
 * TODO: drive the UHF sync state machine (uhf_state_t / uhf_transition_state_t) from here
 */
static void uhf_pusp_rx(const uint8_t *data, size_t len) {
    if (len < sizeof(pusp_packet_t)) {
        warning("Radio: dropping %u byte PUSP packet\n", len);
        return;
    }
    pusp_packet_t pkt;
    memcpy(&pkt, data, sizeof(pkt));
    debug("Radio: received PUSP directive %u\n", pkt.dir);
}

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

/**
 * Reads a received frame from the BBC0 RX buffer, checks its space packet, and hands the packet's data to whatever
 * handles its APID (CFDP PDUs go on the uplink queue, PUSP packets are handled by this task).
 */
void uhf_rx_process() {
    uint8_t pc = 0;
    if (at86rf215_reg_read_8(&at86_dev, &pc, REG_BBC0_PC) || !(pc & AT86_BBC_PC_FCSOK)) {
        warning("Radio: dropping frame with bad FCS\n");
        return;
    }

    // TODO: Raise radio task priority
    // Question: do we need to, or can it just always be highest priority that is otherwise blocked.

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

    uslp_transfer_frame_view_t tframe_view;
    if (uslp_transfer_frame_parse(&tframe_view, at86_buffer, (uint32_t)sz)) {
        warning("Radio: dropping malformed USLP frame\n");
        return;
    }

    if (!radio_route_accepts_uplink(RADIO_UHF, tframe_view.primary_header.virtual_channel_id)) {
        warning("Radio: dropping USLP frame on unexpected VC %u\n", tframe_view.primary_header.virtual_channel_id);
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

    if (len == 0 || len > sizeof(item.data)) {
        warning("Radio: dropping space packet with a %u byte data field\n", len);
        return;
    }

    switch (packet_view.header.application_process_id) {
        case CCSDS_APID_CFDP:
            item.len = (uint16_t)len;
            item.apid = CCSDS_APID_CFDP;
            item.vcid = tframe_view.primary_header.virtual_channel_id;
            memcpy(item.data, packet_view.data, len);
            if (xQueueSend(uplink_queue, &item, 0) != pdPASS) {
                warning("Radio: uplink queue full, dropping packet\n");
            }
            break;
        case CCSDS_APID_PUSP:
            uhf_pusp_rx(packet_view.data, len);
            break;
        default:
            warning("Radio: dropping space packet with unknown APID 0x%x\n", packet_view.header.application_process_id);
            break;
    }
}

/**
 * Starts sending the next downlink packet. The radio must be in TXPREP.
 * Returns true if more packets are waiting.
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
            int ret = at86_send_frame(&at86_dev, at86_buffer, frame_sz);
            if (ret) {
                warning("Radio: failed to send frame (%d)\n", ret);
            }
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
 * Sends cmd=TXPREP, successful fires TXRDY IRQ
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
    radio->irqmm = 0;
    radio->irqp = 0;
    radio->pad_drv = AT86RF215_RF_DRV2;
    int ret = at86rf215_init(radio);
    if (ret) {
        warning("AT86: init failed (%d)\n", ret);
        return ret;
    }

    // at86rf215_init() unmasks TXFE and TRXRDY on both radios. Unmask the RF09 IRQs that the task handles, and mask all
    // RF24/BBC1 IRQs: the task never reads their IRQS registers, so a pending one would hold the IRQ pin active
    ret = at86rf215_set_bbc_irq_mask(radio, AT86RF215_RF09, AT86_BBC_IRQ_RXFS | AT86_BBC_IRQ_RXFE | AT86_BBC_IRQ_TXFE);
    if (!ret) {
        ret = at86rf215_set_radio_irq_mask(radio, AT86RF215_RF09, AT86_RF_IRQ_WAKEUP | AT86_RF_IRQ_TRXRDY | AT86_RF_IRQ_TRXERR);
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
 * \return true if it got there within UHF_STATE_TIMEOUT_MS
 */
static bool uhf_wait_txprep(void) {
    const TickType_t start = xTaskGetTickCount();
    do {
        at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;
        if (at86rf215_get_state(&at86_dev, &state, AT86RF215_RF09) == AT86RF215_OK && state == AT86RF215_STATE_RF_TXPREP) {
            return true;
        }
        vTaskDelay(1);
    } while (xTaskGetTickCount() - start < pdMS_TO_TICKS(UHF_STATE_TIMEOUT_MS));
    return false;
}

bool uhf_chip_setup(void) {
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

    UHF_SETUP_TRY(at86rf215_set_cmd(&at86_dev, AT86RF215_CMD_RF_TXPREP, AT86RF215_RF09));
    if (!uhf_wait_txprep()) {
        warning("Radio: did not reach TXPREP\n");
        return false;
    }

    at86rf215_pll_ls_t pll = AT86RF215_PLL_UNLOCKED;
    UHF_SETUP_TRY(at86rf215_get_pll_ls(&at86_dev, &pll, AT86RF215_RF09));
    if (pll != AT86RF215_PLL_LOCKED) {
        warning("Radio: PLL did not lock\n");
        return false;
    }

    // Reaching TXPREP left TRXRDY pending, which holds the IRQ pin active
    UHF_SETUP_TRY(at86rf215_irq_clear(&at86_dev));
    UHF_SETUP_TRY(uhf_go_rx());

    info("Radio: chip set up (%lu Hz, power %d), listening\n", (unsigned long)UHF_FREQ_HZ, UHF_TX_POWER);
    return true;
}
