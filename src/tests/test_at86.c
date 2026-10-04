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

    // Flip every bit so the new value can never match what was already there
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
    test_log("----- testing at86rf215 -----\n");

    // The SPI peripheral is initialized by Atmel Start but not enabled anywhere else
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
