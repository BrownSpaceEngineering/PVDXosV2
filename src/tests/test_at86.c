/**
 * test_at86.c
 *
 * Hardware tests for the AT86RF215 radio driver. These talk to the real chip over SPI_UHF, so they only pass
 * on a board with the radio attached.
 *
 * Clauded test file, do not merge to main
 */

#include "test_at86.h"

#include <driver_init.h>
#include <hal_delay.h>

#include "drivers/at86rf215/at86rf215.h"
#include "drivers/at86rf215/regs.h"
#include "logging.h"
#include "tests/test.h"

#ifdef UNITTEST
/**
 * \fn at86rf215_delay_us
 *
 * \brief Overrides the driver's weak delay, which uses vTaskDelay. Tests run before the scheduler starts, where
 *      vTaskDelay is not usable, so busy-wait instead
 */
void at86rf215_delay_us(struct at86rf215 *h, uint32_t us) {
    (void)h;
    while (us > UINT16_MAX) {
        delay_us(UINT16_MAX);
        us -= UINT16_MAX;
    }
    delay_us((uint16_t)us);
}
#endif

static struct at86rf215 at86_dev;

/**
 * \fn test_at86_cs_write
 *
 * \brief Writes a new value to an RFn_CS (channel spacing) register, reads it back, then restores the original value
 */
static void test_at86_cs_write(const char *name, uint16_t reg) {
    uint8_t original = 0;
    int ret = at86rf215_reg_read_8(&at86_dev, &original, reg);
    test_log("%s_CS original: 0x%02x (ret %d)\n", name, original, ret);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "CS read\n");

    const uint8_t expected = (uint8_t)~original;
    ret = at86rf215_reg_write_8(&at86_dev, expected, reg);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "CS write\n");

    uint8_t readback = 0;
    ret = at86rf215_reg_read_8(&at86_dev, &readback, reg);
    test_log("%s_CS wrote 0x%02x, read back 0x%02x (ret %d)\n", name, expected, readback, ret);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "CS readback\n");
    PVDX_ASSERT_MSG(readback == expected, "CS readback matches written value\n");

    ret = at86rf215_reg_write_8(&at86_dev, original, reg);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "CS restore\n");
}

void test_at86(void) {
    spi_m_sync_enable(&SPI_UHF);

    at86_dev.clko_os = AT86RF215_RF_CLKO_OFF;
    at86_dev.clk_drv = AT86RF215_RF_DRVCLKO2;
    at86_dev.rf_femode_09 = AT86RF215_RF_FEMODE0;
    at86_dev.rf_femode_24 = AT86RF215_RF_FEMODE0;
    at86_dev.xo_fs = 0;
    at86_dev.xo_trim = 0;
    at86_dev.irqmm = 0;
    at86_dev.irqp = 0;
    at86_dev.pad_drv = AT86RF215_RF_DRV2;

    test_log("at86rf215 init test:\n");
    int ret = at86rf215_init(&at86_dev);
    test_log("at86rf215_init ret: %d\n", ret);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "at86rf215_init\n");

    ret = at86rf215_conn_check(&at86_dev);
    test_log("at86rf215_conn_check ret: %d (family 0x%02x, version 0x%02x)\n", ret, at86_dev.priv.family, at86_dev.priv.version);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "at86rf215_conn_check\n");

    test_log("at86rf215 CS register write test:\n");
    test_at86_cs_write("RF09", REG_RF09_CS);
    test_at86_cs_write("RF24", REG_RF24_CS);
}

#ifdef TEST_AT86_TX
    #include <string.h>

    #define TX_FREQ_HZ      437765000UL
    #define TX_POWER        10 
    #define TX_PAYLOAD_LEN  64
    #define TX_SPIN_LIMIT   2000000UL
    #define TX_LOG_INTERVAL 50

static const char lorem[] = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore.";

/**
 * \fn tx_wait_state
 *
 * \brief Spins until RF09 reaches \p want. Bounded by a spin count because the tick based timeouts in the driver do not run
 *      before the scheduler starts
 */
static bool tx_wait_state(at86rf215_rf_state_t want) {
    at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;
    for (uint32_t i = 0; i < TX_SPIN_LIMIT; ++i) {
        if (at86rf215_get_state(&at86_dev, &state, AT86RF215_RF09) == AT86RF215_OK && state == want) {
            return true;
        }
        at86rf215_delay_us(&at86_dev, 10);
    }
    return false;
}

/**
 * \fn tx_send_psdu
 *
 * \brief Loads one PSDU into the BBC0 TX frame buffer over SPI, starts the transmission and waits for TXFE
 *
 * \return AT86RF215_OK on success, negative driver error or -AT86RF215_TIMEOUT otherwise
 */
static int tx_send_psdu(const uint8_t *psdu, size_t len) {
    // The chip appends the 2 byte FCS (BBC0_PC.TXAFCS), the frame length must cover it
    const size_t frame_len = len + 2;
    int ret = at86rf215_reg_write_8(&at86_dev, frame_len & 0xFF, REG_BBC0_TXFLL);
    if (ret) return ret;
    ret = at86rf215_reg_write_8(&at86_dev, (frame_len >> 8) & 0xFF, REG_BBC0_TXFLH);
    if (ret) return ret;

    // Burst write into the frame buffer: address with the write bit set, then the data, in one buffer.
    // at86rf215_spi_write() drives chip select itself, so header and data must go out in a single call
    // or the chip reads the first two data bytes as a new register address
    // The buffer must hold the whole PSDU including the 2 FCS octets even with TXAFCS set (the chip overwrites them)
    uint8_t burst[2 + TX_PAYLOAD_LEN + 2];
    if (len > TX_PAYLOAD_LEN) return -AT86RF215_INVAL_PARAM;
    burst[0] = (REG_BBC0_FBTXS >> 8) | 0x80;
    burst[1] = REG_BBC0_FBTXS & 0xFF;
    memcpy(&burst[2], psdu, len);
    memset(&burst[2 + len], 0, 2);
    ret = at86rf215_spi_write(&at86_dev, burst, 2 + frame_len);
    if (ret) return ret;

    uint8_t irqs = 0;
    ret = at86rf215_reg_read_8(&at86_dev, &irqs, REG_BBC0_IRQS);
    if (ret) return ret;

    ret = at86rf215_set_cmd(&at86_dev, AT86RF215_CMD_RF_TX, AT86RF215_RF09);
    if (ret) return ret;

    for (uint32_t i = 0; i < TX_SPIN_LIMIT; ++i) {
        ret = at86rf215_reg_read_8(&at86_dev, &irqs, REG_BBC0_IRQS);
        if (ret) return ret;
        if (irqs & (1 << 4)) { // TXFE, frame fully transmitted
            return AT86RF215_OK;
        }
    }
    return -AT86RF215_TIMEOUT;
}

/**
 * \fn test_at86_tx
 *
 * Tests sending legacy O-QPSK frames
 */
void test_at86_tx(void) {
    spi_m_sync_enable(&SPI_UHF);

    at86_dev.clko_os = AT86RF215_RF_CLKO_OFF;
    at86_dev.clk_drv = AT86RF215_RF_DRVCLKO2;
    at86_dev.rf_femode_09 = AT86RF215_RF_FEMODE1;
    at86_dev.rf_femode_24 = AT86RF215_RF_FEMODE0;
    at86_dev.xo_fs = 0;
    at86_dev.xo_trim = 0;
    at86_dev.irqmm = 0;
    at86_dev.irqp = 0;
    at86_dev.pad_drv = AT86RF215_RF_DRV2;

    int ret = at86rf215_init(&at86_dev);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "tx init\n");
    ret = at86rf215_conn_check(&at86_dev);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "tx conn_check\n");
    ret = at86rf215_set_mode(&at86_dev, AT86RF215_RF_MODE_BBRF);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "tx set_mode\n");

    const struct at86rf215_radio_conf radio_conf = {
        .cm = AT86RF215_CM_FINE_RES_04,
        .cs = 0,
        .base_freq = 0,
        .lbw = AT86RF215_PLL_LBW_DEFAULT,
    };
    ret = at86rf215_radio_conf(&at86_dev, AT86RF215_RF09, &radio_conf);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "tx radio_conf\n");

    struct at86rf215_bb_conf bb;
    memset(&bb, 0, sizeof(bb));
    bb.pt = AT86RF215_BB_MROQPSK;
    bb.fcst = AT86RF215_FCS_16;
    bb.txafcs = 1;
    bb.qpsk.fchip = AT86RF215_MROQPSK_FCHIP_2000;
    bb.qpsk.fshape_mod = AT86RF215_MROQPSK_BB_RRC08;
    bb.qpsk.drate_mod = AT86RF215_MROQPSK_DRATE_MOD_0;
    bb.qpsk.leg = 1;     // OQPSKPHRTX.LEG: transmit legacy O-QPSK
    bb.qpsk.fcstleg = 1; // OQPSKC2.FCSTLEG: 16 bit FCS for legacy
    bb.qpsk.rxm = AT86RF215_MROQPSK_RXM_LEGACY_ONLY;
    bb.qpsk.nsfd = AT86RF215_MROQPSK_NSFD_SFD1;
    ret = at86rf215_bb_conf(&at86_dev, AT86RF215_RF09, &bb);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "tx bb_conf\n");
    ret = at86rf215_bb_enable(&at86_dev, AT86RF215_RF09, 1);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "tx bb_enable\n");

    ret = at86rf215_set_freq(&at86_dev, AT86RF215_RF09, TX_FREQ_HZ);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "tx set_freq\n");
    ret = at86rf215_set_pac(&at86_dev, AT86RF215_RF09, AT86RF215_PACUR_NO_RED, TX_POWER);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "tx set_pac\n");

    ret = at86rf215_set_cmd(&at86_dev, AT86RF215_CMD_RF_TXPREP, AT86RF215_RF09);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK, "tx cmd TXPREP\n");
    PVDX_ASSERT_MSG(tx_wait_state(AT86RF215_STATE_RF_TXPREP), "tx reached TXPREP\n");

    at86rf215_pll_ls_t pll = AT86RF215_PLL_UNLOCKED;
    ret = at86rf215_get_pll_ls(&at86_dev, &pll, AT86RF215_RF09);
    test_log("tx pll ret %d, locked %d\n", ret, (int)pll);
    PVDX_ASSERT_MSG(ret == AT86RF215_OK && pll == AT86RF215_PLL_LOCKED, "tx pll locked\n");

    test_log("tx start: %lu Hz, power %d, %d byte payload\n", (unsigned long)TX_FREQ_HZ, TX_POWER, TX_PAYLOAD_LEN);

    uint8_t psdu[TX_PAYLOAD_LEN];
    uint32_t sent = 0;
    for (;;) {
        psdu[0] = (uint8_t)sent;
        for (size_t i = 1; i < sizeof(psdu); ++i) {
            psdu[i] = (uint8_t)lorem[(i - 1 + sent) % (sizeof(lorem) - 1)];
        }

        ret = tx_send_psdu(psdu, sizeof(psdu));
        if (ret != AT86RF215_OK) {
            warning("tx frame %lu failed: %d\n", (unsigned long)sent, ret);
        }

        at86rf215_set_cmd(&at86_dev, AT86RF215_CMD_RF_TXPREP, AT86RF215_RF09);
        if (!tx_wait_state(AT86RF215_STATE_RF_TXPREP)) {
            warning("tx did not return to TXPREP\n");
        }

        ++sent;
        if (sent % TX_LOG_INTERVAL == 0) {
            test_log("tx frames sent: %lu\n", (unsigned long)sent);
        }
    }
}
#endif // TEST_AT86_TX
