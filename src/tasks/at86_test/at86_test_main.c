/**
 * at86_test_main.c
 *
 * "p/x at86_test" "p at86_dev.priv"
 * "set var at86_test.mbox.addr = 0x0D" "set var at86_test.mbox.op = 1"
 * "p/x at86_test.mbox"
 *
 * Clauded test file, do not merge to main
 */

#include "drivers/at86rf215/regs.h"
#include "logging.h"
#include "tasks/at86_test/at86_test_task.h"
#include "tasks/command_dispatcher/command_dispatcher_task.h"

#define AT86_TEST_LOOP_PERIOD_MS 500

at86_test_task_memory_t at86_test_mem;
struct at86rf215 at86_dev;
volatile at86_test_state_t at86_test;

// Register addresses of one RF front end and its baseband core, indexed in the same order as at86_radio_regs_t
typedef struct {
    uint16_t irqm, auxs, state, cmd, cs, ccf0l, ccf0h, cnl, cnm, rxbwc, agcc, agcs, rssi, txcutc, pac, padfe, pll;
    uint16_t bbc_irqm, bbc_pc;
} at86_radio_addrs_t;

static const at86_radio_addrs_t radio_addrs[2] = {
    [AT86RF215_RF09] = {REG_RF09_IRQM, REG_RF09_AUXS, REG_RF09_STATE, REG_RF09_CMD, REG_RF09_CS, REG_RF09_CCF0L, REG_RF09_CCF0H,
                        REG_RF09_CNL, REG_RF09_CNM, REG_RF09_RXBWC, REG_RF09_AGCC, REG_RF09_AGCS, REG_RF09_RSSI, REG_RF09_TXCUTC,
                        REG_RF09_PAC, REG_RF09_PADFE, REG_RF09_PLL, REG_BBC0_IRQM, REG_BBC0_PC},
    [AT86RF215_RF24] = {REG_RF24_IRQM, REG_RF24_AUXS, REG_RF24_STATE, REG_RF24_CMD, REG_RF24_CS, REG_RF24_CCF0L, REG_RF24_CCF0H,
                        REG_RF24_CNL, REG_RF24_CNM, REG_RF24_RXBWC, REG_RF24_AGCC, REG_RF24_AGCS, REG_RF24_RSSI, REG_RF24_TXCUTC,
                        REG_RF24_PAC, REG_RF24_PADFE, REG_RF24_PLL, REG_BBC1_IRQM, REG_BBC1_PC},
};

/**
 * \fn read_reg
 *
 * \brief Reads one 8-bit register into a volatile destination, recording the first error in `*p_ret`
 */
static void read_reg(volatile uint8_t *p_dst, uint16_t reg, int32_t *p_ret) {
    uint8_t val = 0;
    int ret = at86rf215_reg_read_8(&at86_dev, &val, reg);
    if (ret == AT86RF215_OK) {
        *p_dst = val;
    } else if (*p_ret == AT86RF215_OK) {
        *p_ret = ret;
    }
}

/**
 * \fn dump_registers
 *
 * \brief Copies the common registers and the registers of both RF front ends into `at86_test`
 *
 * \return the first nonzero driver return code, or 0 if all reads succeeded
 */
static int32_t dump_registers(void) {
    int32_t ret = AT86RF215_OK;

    read_reg(&at86_test.rst, REG_RF_RST, &ret);
    read_reg(&at86_test.cfg, REG_RF_CFG, &ret);
    read_reg(&at86_test.clko, REG_RF_CLKO, &ret);
    read_reg(&at86_test.bmdvc, REG_RF_BMDVC, &ret);
    read_reg(&at86_test.xoc, REG_RF_XOC, &ret);
    read_reg(&at86_test.iqifc0, REG_RF_IQIFC0, &ret);
    read_reg(&at86_test.iqifc1, REG_RF_IQIFC1, &ret);
    read_reg(&at86_test.iqifc2, REG_RF_IQIFC2, &ret);
    read_reg(&at86_test.pn, REG_RF_PN, &ret);
    read_reg(&at86_test.vn, REG_RF_VN, &ret);

    for (int i = 0; i < 2; i++) {
        volatile at86_radio_regs_t *const r = &at86_test.radio[i];
        const at86_radio_addrs_t *const a = &radio_addrs[i];

        read_reg(&r->irqm, a->irqm, &ret);
        read_reg(&r->auxs, a->auxs, &ret);
        read_reg(&r->state, a->state, &ret);
        read_reg(&r->cmd, a->cmd, &ret);
        read_reg(&r->cs, a->cs, &ret);
        read_reg(&r->ccf0l, a->ccf0l, &ret);
        read_reg(&r->ccf0h, a->ccf0h, &ret);
        read_reg(&r->cnl, a->cnl, &ret);
        read_reg(&r->cnm, a->cnm, &ret);
        read_reg(&r->rxbwc, a->rxbwc, &ret);
        read_reg(&r->agcc, a->agcc, &ret);
        read_reg(&r->agcs, a->agcs, &ret);
        read_reg(&r->rssi, a->rssi, &ret);
        read_reg(&r->txcutc, a->txcutc, &ret);
        read_reg(&r->pac, a->pac, &ret);
        read_reg(&r->padfe, a->padfe, &ret);
        read_reg(&r->pll, a->pll, &ret);
        read_reg(&r->bbc_irqm, a->bbc_irqm, &ret);
        read_reg(&r->bbc_pc, a->bbc_pc, &ret);
    }

    return ret;
}

/**
 * \fn service_mailbox
 *
 * \brief Performs the register access requested through `at86_test.mbox`, if any
 */
static void service_mailbox(void) {
    const uint32_t op = at86_test.mbox.op;
    if (op == AT86_MBOX_IDLE) {
        return;
    }

    const uint16_t addr = at86_test.mbox.addr;
    int ret;

    switch (op) {
        case AT86_MBOX_READ8:
            {
                uint8_t val = 0;
                ret = at86rf215_reg_read_8(&at86_dev, &val, addr);
                at86_test.mbox.value = val;
                break;
            }
        case AT86_MBOX_WRITE8:
            ret = at86rf215_reg_write_8(&at86_dev, (uint8_t)at86_test.mbox.value, addr);
            break;
        case AT86_MBOX_READ32:
            {
                uint32_t val = 0;
                ret = at86rf215_reg_read_32(&at86_dev, &val, addr);
                at86_test.mbox.value = val;
                break;
            }
        default:
            ret = -AT86RF215_INVAL_PARAM;
            break;
    }

    info("at86_test: mailbox op %lu addr 0x%03x value 0x%08lx ret %d\n", op, addr, at86_test.mbox.value, ret);

    at86_test.mbox.ret = ret;
    at86_test.mbox.count++;
    at86_test.mbox.op = AT86_MBOX_IDLE; // Cleared last so GDB sees a complete result once op reads back as idle
}

/**
 * \fn main_at86_test
 *
 * \param pvParameters a void pointer to the parameters required by the AT86RF215 test task;
 *      not currently set by config
 *
 * \warning should never return
 */
void main_at86_test(void *pvParameters) {
    info("at86_test: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);

    // The SPI peripheral is initialized by Atmel Start but not enabled anywhere else
    spi_m_sync_enable(&SPI_UHF);

    // Done here rather than in an init function because the driver's delay hook uses vTaskDelay
    at86_dev.clko_os = AT86RF215_RF_CLKO_OFF;
    at86_dev.clk_drv = AT86RF215_RF_DRVCLKO2;
    at86_dev.rf_femode_09 = AT86RF215_RF_FEMODE0;
    at86_dev.rf_femode_24 = AT86RF215_RF_FEMODE0;
    at86_dev.xo_fs = 0;
    at86_dev.xo_trim = 0;
    at86_dev.irqmm = 0;
    at86_dev.irqp = 0;
    at86_dev.pad_drv = AT86RF215_RF_DRV2;

    at86_test.init_ret = at86rf215_init(&at86_dev);
    if (at86_test.init_ret != AT86RF215_OK) {
        warning("at86_test: at86rf215_init failed (%d)\n", at86_test.init_ret);
    }
    at86_test.conn_ret = at86rf215_conn_check(&at86_dev);
    if (at86_test.conn_ret != AT86RF215_OK) {
        warning("at86_test: at86rf215_conn_check failed (%d)\n", at86_test.conn_ret);
    }
    if (at86_test.init_ret == AT86RF215_OK && at86_test.conn_ret == AT86RF215_OK) {
        info("at86_test: AT86RF215 initialized (family 0x%02x, version 0x%02x)\n", at86_dev.priv.family, at86_dev.priv.version);
    }

    while (true) {
        service_mailbox();

        at86_test.last_ret = dump_registers();
        if (at86_test.last_ret != AT86RF215_OK) {
            debug("at86_test: register dump failed (%d)\n", at86_test.last_ret);
        }
        at86_test.loop_count++;

        vTaskDelay(pdMS_TO_TICKS(AT86_TEST_LOOP_PERIOD_MS));

        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("at86_test: Enqueued watchdog checkin command\n");
        }
    }
}
