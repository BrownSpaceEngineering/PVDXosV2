/*
 *  at86rf215-driver: OS-independent driver for the AT86RF215 transceiver
 *
 *  Copyright (C) 2020-2024, Libre Space Foundation <http://libre.space>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "at86rf215.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "at86rf215_helpers.h"
#include "globals.h"
#include "hal_spi_m_sync.h"
#include "regs.h"

static uint8_t spi_buffer[UHF_SPI_BUF_SIZE];

// Preset buffers for the driver
//
// I know their driver just uses the above spi buffer but I like this more.
uint8_t uhf_spi_rx_buffer[UHF_SPI_BUF_SIZE] = {0x00};
uint8_t uhf_spi_tx_buffer[UHF_SPI_BUF_SIZE] = {0x00};

#define INIT_MAGIC_VAL 0x92c2f0e3

#ifndef max
    #define max(a, b)                                                                                                                      \
        ({                                                                                                                                 \
            __typeof__(a) _a = (a);                                                                                                        \
            __typeof__(b) _b = (b);                                                                                                        \
            _a > _b ? _a : _b;                                                                                                             \
        })
#endif

#ifndef min
    #define min(a, b)                                                                                                                      \
        ({                                                                                                                                 \
            __typeof__(a) _a = (a);                                                                                                        \
            __typeof__(b) _b = (b);                                                                                                        \
            _a < _b ? _a : _b;                                                                                                             \
        })
#endif

#define BIT(n) (1UL << (n))

/* Table 6.60 to 6.63 definitions */

static const at86rf215_rcut_t fsk_tx_conf_rcut_midx1[6] = {AT86RF215_RCUT_25FS2, AT86RF215_RCUT_25FS2, AT86RF215_RCUT_25FS2,
                                                           AT86RF215_RCUT_25FS2, AT86RF215_RCUT_25FS2, AT86RF215_RCUT_25FS2};
static const at86rf215_rcut_t fsk_tx_conf_rcut_midx3[6] = {AT86RF215_RCUT_100FS2, AT86RF215_RCUT_100FS2, AT86RF215_RCUT_100FS2,
                                                           AT86RF215_RCUT_100FS2, AT86RF215_RCUT_100FS2, AT86RF215_RCUT_100FS2};

static const at86rf215_rcut_t fsk_rx_conf_rcut_midx1_09[6] = {AT86RF215_RCUT_25FS2, AT86RF215_RCUT_25FS2, AT86RF215_RCUT_25FS2,
                                                              AT86RF215_RCUT_37FS2, AT86RF215_RCUT_25FS2, AT86RF215_RCUT_25FS2};
static const at86rf215_rcut_t fsk_rx_conf_rcut_midx1_24[6] = {AT86RF215_RCUT_25FS2, AT86RF215_RCUT_25FS2, AT86RF215_RCUT_25FS2,
                                                              AT86RF215_RCUT_37FS2, AT86RF215_RCUT_25FS2, AT86RF215_RCUT_37FS2};

static const at86rf215_rcut_t fsk_rx_conf_rcut_midx3_09[6] = {AT86RF215_RCUT_37FS2, AT86RF215_RCUT_37FS2, AT86RF215_RCUT_37FS2,
                                                              AT86RF215_RCUT_50FS2, AT86RF215_RCUT_37FS2, AT86RF215_RCUT_37FS2};
static const at86rf215_rcut_t fsk_rx_conf_rcut_midx3_24[6] = {AT86RF215_RCUT_37FS2, AT86RF215_RCUT_37FS2, AT86RF215_RCUT_50FS2,
                                                              AT86RF215_RCUT_75FS2, AT86RF215_RCUT_37FS2, AT86RF215_RCUT_50FS2};

static const at86rf215_rx_bw_t fsk_rx_conf_rbw_midx1_09[6] = {AT86RF215_RF_BW160KHZ_IF250KHZ, AT86RF215_RF_BW200KHZ_IF250KHZ,
                                                              AT86RF215_RF_BW320KHZ_IF500KHZ, AT86RF215_RF_BW320KHZ_IF500KHZ,
                                                              AT86RF215_RF_BW500KHZ_IF500KHZ, AT86RF215_RF_BW630KHZ_IF1000KHZ};
static const at86rf215_rx_bw_t fsk_rx_conf_rbw_midx1_24[6] = {AT86RF215_RF_BW160KHZ_IF250KHZ,  AT86RF215_RF_BW200KHZ_IF250KHZ,
                                                              AT86RF215_RF_BW320KHZ_IF500KHZ,  AT86RF215_RF_BW400KHZ_IF500KHZ,
                                                              AT86RF215_RF_BW630KHZ_IF1000KHZ, AT86RF215_RF_BW800KHZ_IF1000KHZ};

static const at86rf215_rx_bw_t fsk_rx_conf_rbw_midx3_09[6] = {AT86RF215_RF_BW160KHZ_IF250KHZ,  AT86RF215_RF_BW320KHZ_IF500KHZ,
                                                              AT86RF215_RF_BW400KHZ_IF500KHZ,  AT86RF215_RF_BW500KHZ_IF500KHZ,
                                                              AT86RF215_RF_BW630KHZ_IF1000KHZ, AT86RF215_RF_BW1000KHZ_IF1000KHZ};
static const at86rf215_rx_bw_t fsk_rx_conf_rbw_midx3_24[6] = {AT86RF215_RF_BW200KHZ_IF250KHZ,  AT86RF215_RF_BW400KHZ_IF500KHZ,
                                                              AT86RF215_RF_BW630KHZ_IF1000KHZ, AT86RF215_RF_BW630KHZ_IF1000KHZ,
                                                              AT86RF215_RF_BW800KHZ_IF1000KHZ, AT86RF215_RF_BW1000KHZ_IF1000KHZ};

static const uint8_t fsk_rx_conf_ifs_midx1_09[6] = {0, 0, 0, 0, 1, 0};
static const uint8_t fsk_rx_conf_ifs_midx1_24[6] = {0, 0, 0, 0, 0, 0};
static const uint8_t fsk_rx_conf_ifs_midx3_09[6] = {0, 0, 0, 1, 0, 1};
static const uint8_t fsk_rx_conf_ifs_midx3_24[6] = {0, 0, 0, 0, 0, 1};

/**
 * Checks if the device structure has been successfully initialized through the
 * at86rf215_init().
 * @param h the device handle
 * @return 0 on success or negative error code
 */
static int ready(struct at86rf215 *h) {
    if (!h) {
        return -AT86RF215_INVAL_PARAM;
    }
    if (h->priv.init != INIT_MAGIC_VAL) {
        return -AT86RF215_NO_INIT;
    }
    return AT86RF215_OK;
}

/**
 * Checks if the RF interface is supported based on the device family
 * @param h the device handle
 * @param radio the RF frontend
 * @return 0 on success or negative error code
 */
static int supports_rf(struct at86rf215 *h, at86rf215_radio_t radio) {
    int ret = ready(h);
    if (ret) {
        return ret;
    }
    switch (radio) {
        case AT86RF215_RF09:
        case AT86RF215_RF24:
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    if (radio == AT86RF215_RF24 && h->priv.family == AT86RF215M) {
        return -AT86RF215_NOT_SUPPORTED;
    }
    return AT86RF215_OK;
}

/**
 * Checks if the baseband mode is supported based on the device family
 * @param h the device handle
 * @param radio the RF frontend
 * @return 0 on success or negative error code
 */
static int supports_mode(struct at86rf215 *h, at86rf215_chpm_t mode) {
    int ret = ready(h);
    if (ret) {
        return ret;
    }
    switch (mode) {
        case AT86RF215_RF_MODE_BBRF:
        case AT86RF215_RF_MODE_RF:
        case AT86RF215_RF_MODE_BBRF09:
        case AT86RF215_RF_MODE_BBRF24:
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    if (h->priv.family == AT86RF215IQ && mode != AT86RF215_RF_MODE_RF) {
        return -AT86RF215_NOT_SUPPORTED;
    }
    if (h->priv.family == AT86RF215M && mode == AT86RF215_RF_MODE_BBRF24) {
        return -AT86RF215_NOT_SUPPORTED;
    }
    return AT86RF215_OK;
}

/**
 * Resets and initializes the AT86RF215 IC
 * @param h the device handle
 * @param drv clock output driving current
 * @param os clock output selection
 * @return 0 on success or negative error code
 */
int at86rf215_init(struct at86rf215 *h) {
    if (!h) {
        return -AT86RF215_INVAL_PARAM;
    }
    /* Reset the state of the private struct members */
    memset(&h->priv, 0, sizeof(struct at86rf215_priv));

    at86rf215_irq_enable(h, 0);
    /* Reset the IC */
    at86rf215_set_rstn(h, 0);
    at86rf215_delay_us(h, 1000);
    at86rf215_set_rstn(h, 1);
    at86rf215_delay_us(h, 1000);

    uint8_t val = 0;
    int ret = at86rf215_reg_read_8(h, &val, REG_RF_PN);
    if (ret) {
        return ret;
    }
    switch (val) {
        case AT86RF215:
        case AT86RF215IQ:
        case AT86RF215M:
            h->priv.family = (at86rf215_family_t)val;
            break;
        default:
            return -AT86RF215_UNKNOWN_IC;
    }

    val = 0;
    switch (h->clk_drv) {
        case AT86RF215_RF_DRVCLKO2:
        case AT86RF215_RF_DRVCLKO4:
        case AT86RF215_RF_DRVCLKO6:
        case AT86RF215_RF_DRVCLKO8:
            val = h->clk_drv << 3;
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    switch (h->clko_os) {
        case AT86RF215_RF_CLKO_OFF:
        case AT86RF215_RF_CLKO_26_MHZ:
        case AT86RF215_RF_CLKO_32_MHZ:
        case AT86RF215_RF_CLKO_16_MHZ:
        case AT86RF215_RF_CLKO_8_MHZ:
        case AT86RF215_RF_CLKO_4_MHZ:
        case AT86RF215_RF_CLKO_2_MHZ:
        case AT86RF215_RF_CLKO_1_MHZ:
            val |= h->clko_os;
            break;
    }
    ret = at86rf215_reg_write_8(h, val, REG_RF_CLKO);
    if (ret) {
        return ret;
    }

    /* Apply XO settings */
    ret = at86rf215_reg_write_8(h, (h->xo_fs << 4) | h->xo_trim, REG_RF_XOC);
    if (ret) {
        return ret;
    }

    /* Set the RF_CFG */
    ret = at86rf215_reg_write_8(h, (h->irqmm << 3) | (h->irqp << 2) | h->pad_drv, REG_RF_CFG);
    if (ret) {
        return ret;
    }

    /* Set RF09_PADFE and RF24_PADFE */
    val = 0;
    switch (h->rf_femode_09) {
        case AT86RF215_RF_FEMODE0:
        case AT86RF215_RF_FEMODE1:
        case AT86RF215_RF_FEMODE2:
        case AT86RF215_RF_FEMODE3:
            val = h->rf_femode_09 << 6;
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    ret = at86rf215_reg_write_8(h, val, REG_RF09_PADFE);
    if (ret) {
        return ret;
    }

    val = 0;
    switch (h->rf_femode_24) {
        case AT86RF215_RF_FEMODE0:
        case AT86RF215_RF_FEMODE1:
        case AT86RF215_RF_FEMODE2:
        case AT86RF215_RF_FEMODE3:
            val = h->rf_femode_24 << 6;
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    ret = at86rf215_reg_write_8(h, val, REG_RF24_PADFE);
    if (ret) {
        return ret;
    }

    /* Get the version of the IC */
    ret = at86rf215_reg_read_8(h, &val, REG_RF_VN);
    if (ret) {
        return ret;
    }
    h->priv.version = val;
    h->priv.chpm = AT86RF215_RF_MODE_BBRF;
    h->priv.init = INIT_MAGIC_VAL;

    /*Enable the IRQs that are necessary for the driver */
    at86rf215_set_bbc_irq_mask(h, AT86RF215_RF09, BIT(4));
    at86rf215_set_bbc_irq_mask(h, AT86RF215_RF24, BIT(4));
    at86rf215_set_radio_irq_mask(h, AT86RF215_RF09, BIT(1));
    at86rf215_set_radio_irq_mask(h, AT86RF215_RF24, BIT(1));

    /* Assert any active IRQ */
    at86rf215_reg_read_8(h, &val, REG_RF09_IRQS);
    at86rf215_reg_read_8(h, &val, REG_RF24_IRQS);
    at86rf215_reg_read_8(h, &val, REG_BBC0_IRQS);
    at86rf215_reg_read_8(h, &val, REG_BBC1_IRQS);

    at86rf215_irq_enable(h, 1);
    return AT86RF215_OK;
}

/**
 * @brief Connectivity check
 *
 * @param h the device handle
 * @return 0 on success, -AT86RF215_UNKNOWN_IC if the IC
 * is not one of the known ones, or other negative error code
 */
int at86rf215_conn_check(struct at86rf215 *h) {
    uint8_t val = 0;
    int ret = at86rf215_reg_read_8(h, &val, REG_RF_PN);
    if (ret) {
        return ret;
    }
    switch (val) {
        case AT86RF215:
        case AT86RF215IQ:
        case AT86RF215M:
            return AT86RF215_OK;
        default:
            return -AT86RF215_UNKNOWN_IC;
    }
}

/**
 * Checks if the at86rf215_radio_init() has been succesfully called for the
 * corresponding RF frontend
 * @param h the device handle
 * @param radio the radio frontend
 * @return 0 on success or negative error code
 */
static int radio_ready(struct at86rf215 *h, at86rf215_radio_t radio) {
    int ret = supports_rf(h, radio);
    if (ret) {
        return ret;
    }
    if (h->priv.radios[radio].init != INIT_MAGIC_VAL) {
        return -AT86RF215_NO_INIT;
    }
    return AT86RF215_OK;
}

/**
 * (Re-)Initializes a RF frontend
 * @param h the device handle
 * @param radio the RF frontend
 * @param conf configuration parameters
 * @return 0 on success or negative error code
 */
int at86rf215_radio_conf(struct at86rf215 *h, at86rf215_radio_t radio, const struct at86rf215_radio_conf *conf) {
    if (!conf) {
        return -AT86RF215_INVAL_PARAM;
    }
    int ret = supports_rf(h, radio);
    if (ret) {
        return ret;
    }
    uint32_t spacing = conf->cs / 25000;
    spacing = min(0xFF, spacing);

    /* Set the channel configuration */
    if (radio == AT86RF215_RF09) {
        switch (conf->cm) {
            case AT86RF215_CM_IEEE:
                {
                    ret = at86rf215_reg_write_8(h, spacing, REG_RF09_CS);
                    if (ret) {
                        return ret;
                    }
                    h->priv.radios[AT86RF215_RF09].cs_reg = spacing;
                    h->priv.radios[AT86RF215_RF09].base_freq = conf->base_freq;
                    ret = at86rf215_reg_write_8(h, spacing, REG_RF09_CS);
                    if (ret) {
                        return ret;
                    }

                    /* Apply base frequency */
                    if (conf->base_freq < 389500000 || conf->base_freq > 1020000000) {
                        return -AT86RF215_INVAL_PARAM;
                    }
                    uint16_t base = conf->base_freq / 25000;
                    ret = at86rf215_reg_write_8(h, base & 0xFF, REG_RF09_CCF0L);
                    if (ret) {
                        return ret;
                    }
                    ret = at86rf215_reg_write_8(h, base >> 8, REG_RF09_CCF0H);
                    if (ret) {
                        return ret;
                    }
                }
            case AT86RF215_CM_FINE_RES_04:
            case AT86RF215_CM_FINE_RES_09:
                break;
            default:
                return -AT86RF215_INVAL_PARAM;
        }
    } else {
        switch (conf->cm) {
            case AT86RF215_CM_IEEE:
                {
                    ret = at86rf215_reg_write_8(h, spacing, REG_RF24_CS);
                    if (ret) {
                        return ret;
                    }
                    h->priv.radios[AT86RF215_RF24].cs_reg = spacing;
                    h->priv.radios[AT86RF215_RF24].base_freq = conf->base_freq;

                    /* Apply base frequency */
                    if (conf->base_freq < 2400000000 || conf->base_freq > 2483500000) {
                        return -AT86RF215_INVAL_PARAM;
                    }
                    /* At 2.4 GHz band the base frequency has a
                     * 1.5 GHz offset
                     */
                    uint16_t base = (conf->base_freq - 1500000000) / 25000;
                    ret = at86rf215_reg_write_8(h, base & 0xFF, REG_RF24_CCF0L);
                    if (ret) {
                        return ret;
                    }
                    ret = at86rf215_reg_write_8(h, base >> 8, REG_RF24_CCF0H);
                    if (ret) {
                        return ret;
                    }
                }
            case AT86RF215_CM_FINE_RES_24:
                break;
            default:
                return -AT86RF215_INVAL_PARAM;
        }
    }

    h->priv.radios[radio].cm = conf->cm;
    h->priv.radios[radio].cs = conf->cs;

    /* PLL loop bandwidth is applicable for the sub-1GHz radio only*/
    if (radio == AT86RF215_RF09) {
        switch (conf->lbw) {
            case AT86RF215_PLL_LBW_DEFAULT:
            case AT86RF215_PLL_LBW_SMALLER:
            case AT86RF215_PLL_LBW_LARGER:
                break;
            default:
                return -AT86RF215_INVAL_PARAM;
        }
        ret = at86rf215_reg_write_8(h, conf->lbw, REG_RF09_PLL);
        if (ret) {
            return ret;
        }
    }
    h->priv.radios[radio].init = INIT_MAGIC_VAL;
    return AT86RF215_OK;
}

/**
 * Controls the state of the RSTN pin
 * @param h the device handle
 * @param enable set to 1 to set the RSTN pin to high, 0 to set it to low
 * @return 0 on success or negative error code
 */
__attribute__((weak)) int at86rf215_set_rstn(struct at86rf215 *h, uint8_t enable) {
    if (enable) {
        UHF_RST_HIGH();
    } else {
        UHF_RST_LOW();
    }
    return AT86RF215_OK;
}

/**
 * Controls the state of the SELN pin
 * @param h the device handle
 * @param enable set to 1 to set the SELN pin to high, 0 to set it to low
 * @return 0 on success or negative error code
 */
__attribute__((weak)) int at86rf215_set_seln(struct at86rf215 *h, uint8_t enable) {
    if (enable) {
        UHF_CS_HIGH();
    } else {
        UHF_CS_LOW();
    }
    return AT86RF215_OK;
}

/**
 * Delays the execution by \p us microseconds
 * @param h the device handle
 * @param us the delay in microseconds
 */
__attribute__((weak)) void at86rf215_delay_us(struct at86rf215 *h, uint32_t us) {
    TickType_t ticks = pdMS_TO_TICKS((us + 999u) / 1000u);
    vTaskDelay(ticks ? ticks : 1);
}

/**
 * @brief Returns the current clock in milliseconds
 * @note this clock can wrap around
 *
 * @param h the device handle
 */
__attribute__((weak)) size_t at86rf215_get_time_ms(struct at86rf215 *h) {
    return (size_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/**
 * Reads from the SPI peripheral
 * @param h the device handle
 * @param out the output buffer to hold MISO response from the SPI peripheral
 * @param in input buffer containing MOSI data
 * @param tx_len the number of the MOSI bytes
 * @param tx_len the number of the MISO bytes
 * @return 0 on success or negative error code
 */
__attribute__((weak)) int at86rf215_spi_read(struct at86rf215 *h, uint8_t *out, const uint8_t *in, size_t tx_len, size_t rx_len) {

    memcpy(uhf_spi_tx_buffer, in, tx_len);
    memset(uhf_spi_tx_buffer + tx_len, 0, rx_len - tx_len);

    struct spi_xfer xfer = {
        .txbuf = uhf_spi_tx_buffer,
        .rxbuf = out,
        .size = rx_len,
    };

    UHF_CS_LOW();

    int32_t response = spi_m_sync_transfer(&SPI_UHF, &xfer);

    UHF_CS_HIGH();

    if (response != (int32_t)rx_len) {
        return -AT86RF215_INVAL_PARAM;
    }
    return AT86RF215_OK;
}

/**
 * Writes to the device using the SPI peripheral
 * @param h the device handle
 * @param in the input buffer
 * @param len the size of the input buffer
 * @return 0 on success or negative error code
 */
__attribute__((weak)) int at86rf215_spi_write(struct at86rf215 *h, const uint8_t *in, size_t len) {
    struct spi_xfer xfer = {
        .txbuf = (uint8_t *)in,
        .rxbuf = uhf_spi_rx_buffer, // Unused
        .size = len,
    };

    UHF_CS_LOW();

    int32_t response = spi_m_sync_transfer(&SPI_UHF, &xfer);

    UHF_CS_HIGH();

    if (response != (int32_t)len) {
        return -AT86RF215_INVAL_PARAM;
    }
    return AT86RF215_OK;
}

/**
 * Reads an 8-bit register
 * @note internally the function uses the at86rf215_set_seln() and
 * at86rf215_spi_read() to accomplish the SPI transaction. Developers should
 * provide a proper implementation of those functions.
 *
 * @param h the device handle
 * @param out pointer to hold the read value
 * @param reg the register to read
 * @return 0 on success or negative error code
 */
int at86rf215_reg_read_8(struct at86rf215 *h, uint8_t *out, uint16_t reg) {
    if (!out) {
        return -AT86RF215_INVAL_PARAM;
    }
    int ret = 0;
    ret = at86rf215_set_seln(h, 0);
    if (ret) {
        return ret;
    }

    at86rf215_irq_enable(h, 0);
    /* Construct properly the MOSI buffer */
    uint8_t mosi[2] = {(reg >> 8) & 0x3F, reg & 0xFF};
    uint8_t miso[3] = {0x0, 0x0, 0x0};
    ret = at86rf215_spi_read(h, miso, mosi, 2, 3);
    if (ret) {
        at86rf215_irq_enable(h, 1);
        at86rf215_set_seln(h, 1);
        return ret;
    }
    *out = miso[2];
    at86rf215_irq_enable(h, 1);
    return at86rf215_set_seln(h, 1);
}

/**
 * Reads an 32-bit register
 * @note internally the function uses the at86rf215_set_seln() and
 * at86rf215_spi_read() to accomplish the SPI transaction. Developers should
 * provide a proper implementation of those functions.
 * @note the result is stored in a MS byte first order
 *
 * @param h the device handle
 * @param out pointer to hold the read value
 * @param reg the register to read
 * @return 0 on success or negative error code
 */
int at86rf215_reg_read_32(struct at86rf215 *h, uint32_t *out, uint16_t reg) {
    if (!out) {
        return -AT86RF215_INVAL_PARAM;
    }
    int ret = 0;
    ret = at86rf215_set_seln(h, 0);
    if (ret) {
        return ret;
    }
    at86rf215_irq_enable(h, 0);
    /* Construct properly the MOSI buffer */
    uint8_t mosi[2] = {(reg >> 8) & 0x3F, reg & 0xFF};
    uint8_t miso[6] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
    ret = at86rf215_spi_read(h, miso, mosi, 2, 6);
    if (ret) {
        at86rf215_set_seln(h, 1);
        at86rf215_irq_enable(h, 1);
        return ret;
    }
    *out = (miso[2] << 24) | (miso[3] << 16) | (miso[4] << 8) | miso[5];
    at86rf215_irq_enable(h, 1);
    return at86rf215_set_seln(h, 1);
}

/**
 * Writes an 8-bit register
 *
 * @note internally the function uses the at86rf215_set_seln() and
 * at86rf215_spi_write() to accomplish the SPI transaction. Developers should
 * provide a proper implementation of those functions.
 *
 * @param h the device handle
 * @param in the value to write
 * @param reg the register to write
 * @return 0 on success or negative error code
 */
int at86rf215_reg_write_8(struct at86rf215 *h, const uint8_t in, uint16_t reg) {
    int ret = 0;
    ret = at86rf215_set_seln(h, 0);
    if (ret) {
        return ret;
    }
    at86rf215_irq_enable(h, 0);
    /* Construct properly the MOSI buffer */
    uint8_t mosi[3] = {(reg >> 8) | 0x80, reg & 0xFF, in};
    ret = at86rf215_spi_write(h, mosi, 3);
    if (ret) {
        at86rf215_set_seln(h, 1);
        at86rf215_irq_enable(h, 1);
        return ret;
    }
    at86rf215_irq_enable(h, 1);
    return at86rf215_set_seln(h, 1);
}

/**
 * Writes an 16-bit register
 *
 * @note internally the function uses the at86rf215_set_seln() and
 * at86rf215_spi_write() to accomplish the SPI transaction. Developers should
 * provide a proper implementation of those functions.
 * @note the value is written in a MS byte first order
 *
 * @param h the device handle
 * @param in the value to write
 * @param reg the register to write
 * @return 0 on success or negative error code
 */
int at86rf215_reg_write_16(struct at86rf215 *h, const uint16_t in, uint16_t reg) {
    int ret = 0;
    ret = at86rf215_set_seln(h, 0);
    if (ret) {
        return ret;
    }
    at86rf215_irq_enable(h, 0);
    /* Construct properly the MOSI buffer */
    uint8_t mosi[4] = {(reg >> 8) | 0x80, reg & 0xFF, in >> 8, in & 0xFF};
    ret = at86rf215_spi_write(h, mosi, 4);
    if (ret) {
        at86rf215_set_seln(h, 1);
        at86rf215_irq_enable(h, 1);
        return ret;
    }
    at86rf215_irq_enable(h, 1);
    return at86rf215_set_seln(h, 1);
}

/**
 * Retrieve the RF state of the transceiver
 * @param h the device handle
 * @param state pointer to store the result
 * @param radio the radio front-end to query its state
 * @return 0 on success or negative error code
 */
int at86rf215_get_state(struct at86rf215 *h, at86rf215_rf_state_t *state, at86rf215_radio_t radio) {
    int ret = supports_rf(h, radio);
    if (ret) {
        return ret;
    }
    if (!state) {
        return -AT86RF215_INVAL_PARAM;
    }
    uint8_t val = 0;
    uint16_t reg = 0;
    switch (radio) {
        case AT86RF215_RF24:
            reg = REG_RF24_STATE;
            break;
        case AT86RF215_RF09:
            reg = REG_RF09_STATE;
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    ret = at86rf215_reg_read_8(h, &val, reg);
    if (ret) {
        return ret;
    }
    /* Sanity check on the returned value */
    val &= 0x7;
    switch (val) {
        case AT86RF215_STATE_RF_TRXOFF:
        case AT86RF215_STATE_RF_TXPREP:
        case AT86RF215_STATE_RF_TX:
        case AT86RF215_STATE_RF_RX:
        case AT86RF215_STATE_RF_TRANSITION:
        case AT86RF215_STATE_RF_RESET:
            *state = (at86rf215_rf_state_t)val;
            break;
        default:
            return -AT86RF215_INVAL_VAL;
    }
    return AT86RF215_OK;
}

/**
 * Issue a command to a specified RF frontend of the IC
 * @param h the device handle
 * @param cmd the command
 * @param radio the radio front-end
 * @return 0 on success or negative error code
 */
int at86rf215_set_cmd(struct at86rf215 *h, at86rf215_rf_cmd_t cmd, at86rf215_radio_t radio) {
    int ret = supports_rf(h, radio);
    if (ret) {
        return ret;
    }
    switch (cmd) {
        case AT86RF215_CMD_RF_NOP:
        case AT86RF215_CMD_RF_SLEEP:
        case AT86RF215_CMD_RF_TRXOFF:
        case AT86RF215_CMD_RF_TXPREP:
        case AT86RF215_CMD_RF_TX:
        case AT86RF215_CMD_RF_RX:
        case AT86RF215_CMD_RF_RESET:
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    uint16_t reg = 0;
    switch (radio) {
        case AT86RF215_RF09:
            reg = REG_RF09_CMD;
            break;
        case AT86RF215_RF24:
            reg = REG_RF24_CMD;
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    return at86rf215_reg_write_8(h, cmd, reg);
}

/**
 * @brief Sets the radio to TRXOFF by taking into account also the
 * [Errata reference 4840]
 *
 * @param h the device handle
 * @param radio the radio front-end
 * @param timeout timeout in miliseconds
 * @return int 0 on success or negative error code
 */
int at86rf215_set_trxoff(struct at86rf215 *h, at86rf215_radio_t radio, size_t timeout_ms) {
    size_t deadline = at86rf215_get_time_ms(h) + timeout_ms;
    at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;

    while (state != AT86RF215_STATE_RF_TRXOFF) {
        at86rf215_delay_us(h, 100);
        if (at86rf215_get_time_ms(h) > deadline) {
            return -AT86RF215_TIMEOUT;
        }
        at86rf215_set_cmd(h, AT86RF215_CMD_RF_TRXOFF, radio);
        at86rf215_delay_us(h, 100);
        at86rf215_get_state(h, &state, radio);
    }
    return AT86RF215_OK;
}

/**
 * Sets the chip mode,
 * @note this function ensures that the SKEWDRV part of the register
 * remains unaffected.
 * @param h the device handle
 * @param mode the chip mode
 * @return 0 on success or negative error code
 */
int at86rf215_set_mode(struct at86rf215 *h, at86rf215_chpm_t mode) {
    int ret = supports_mode(h, mode);
    if (ret) {
        return ret;
    }

    uint8_t val = 0;
    ret = at86rf215_reg_read_8(h, &val, REG_RF_IQIFC1);
    if (ret) {
        return ret;
    }
    val = (val & 0x3) | (mode << 4);
    ret = at86rf215_reg_write_8(h, val, REG_RF_IQIFC1);
    if (ret) {
        return ret;
    }
    h->priv.chpm = mode;
    return AT86RF215_OK;
}

/**
 * Resets a specific transceiver
 *
 * @warning This will reset all registers of the specific frontend at their
 * default states
 *
 * @param h the device handle
 * @param radio the transceiver to reset
 * @return 0 on success or negative error code
 */
int at86rf215_transceiver_reset(struct at86rf215 *h, at86rf215_radio_t radio) {
    return at86rf215_set_cmd(h, AT86RF215_CMD_RF_RESET, radio);
}

/**
 * @brief Sets the IRQ mask for the baseband core. 1 activates the
 * corresponding IRQ, 0 deactivates it.
 * @param h the device handle
 * @param radio the RF frontend
 * @param mask the activation mask
 * @return 0 on success or negative error code
 */
int at86rf215_set_bbc_irq_mask(struct at86rf215 *h, at86rf215_radio_t radio, uint8_t mask) {
    int ret = supports_rf(h, radio);
    if (ret) {
        return ret;
    }
    uint16_t reg = 0x0;
    switch (radio) {
        case AT86RF215_RF09:
            reg = REG_BBC0_IRQM;
            break;
        case AT86RF215_RF24:
            reg = REG_BBC1_IRQM;
            break;
        default:
            return -AT86RF215_NOT_SUPPORTED;
    }
    return at86rf215_reg_write_8(h, mask, reg);
}

/**
 * Set the mask for IRQs originating from the RF frontends. 1 activates the
 * corresponding IRQ, 0 deactivates it.
 * @param h the device handle
 * @param radio the RF frontend
 * @param mask the activation mask
 * @return 0 on success or negative error code
 */
int at86rf215_set_radio_irq_mask(struct at86rf215 *h, at86rf215_radio_t radio, uint8_t mask) {
    int ret = supports_rf(h, radio);
    if (ret) {
        return ret;
    }
    uint16_t reg = 0x0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_IRQM;
    } else {
        reg = REG_RF24_IRQM;
    }
    return at86rf215_reg_write_8(h, mask, reg);
}

/**
 * @brief Check the validity of the low pass filter setting
 *
 * @param lpf the low pass filter setting
 * @return true if the setting is valid, false otherwise
 */
static bool is_lpf_valid(at86rf215_lpfcut_t lpf) {
    switch (lpf) {
        case AT86RF215_RF_FLC80KHZ:
        case AT86RF215_RF_FLC100KHZ:
        case AT86RF215_RF_FLC125KHZ:
        case AT86RF215_RF_FLC160KHZ:
        case AT86RF215_RF_FLC200KHZ:
        case AT86RF215_RF_FLC250KHZ:
        case AT86RF215_RF_FLC315KHZ:
        case AT86RF215_RF_FLC400KHZ:
        case AT86RF215_RF_FLC500KHZ:
        case AT86RF215_RF_FLC624KHZ:
        case AT86RF215_RF_FLC800KHZ:
        case AT86RF215_RF_FLC1000KHZ:
            return 1;
    }
    return 0;
}

/**
 * @brief Checks the validity of the power amplifier setting
 *
 * @param paramp the power amplifier setting
 * @return true if the setting is valid, false otherwise
 */
static bool is_paramp_valid(at86rf215_paramp_t paramp) {
    switch (paramp) {
        case AT86RF215_RF_PARAMP4U:
        case AT86RF215_RF_PARAMP8U:
        case AT86RF215_RF_PARAMP16U:
        case AT86RF215_RF_PARAMP32U:
            return 1;
    }
    return 0;
}

/**
 * Sets the PA ramping timing and the LPF configuration for the TX.
 * @param h the device handle
 * @param radio the RF frontend
 * @param paramp PA ramping setting
 * @param lpf LPF setting
 * @return 0 on success or negative error code
 */
int at86rf215_set_txcutc(struct at86rf215 *h, at86rf215_radio_t radio, at86rf215_paramp_t paramp, at86rf215_lpfcut_t lpf) {
    int ret = supports_rf(h, radio);
    if (ret) {
        return ret;
    }
    if (!is_lpf_valid(lpf) || !is_paramp_valid(paramp)) {
        return -AT86RF215_INVAL_PARAM;
    }
    uint16_t reg = 0x0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_TXCUTC;
    } else {
        reg = REG_RF24_TXCUTC;
    }
    return at86rf215_reg_write_8(h, paramp << 6 | lpf, reg);
}

/**
 * Configures the TX digital frontend parameters using the TXDFE register.
 * @param h the device handle
 * @param radio the RF frontend
 * @param rcut TX filter relative to the cut-off frequency
 * @param dm set 1 to enable the direct modulation, 0 otherwise
 * @param sr the sampling rate setting
 * @return 0 on success or negative error code
 */
static int set_txdfe(struct at86rf215 *h, at86rf215_radio_t radio, at86rf215_rcut_t rcut, uint8_t dm, at86rf215_sr_t sr) {
    uint16_t reg = 0x0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_TXDFE;
    } else {
        reg = REG_RF24_TXDFE;
    }
    switch (sr) {
        case AT86RF215_SR_4000KHZ:
        case AT86RF215_SR_2000KHZ:
        case AT86RF215_SR_1333KHZ:
        case AT86RF215_SR_1000KHZ:
        case AT86RF215_SR_800KHZ:
        case AT86RF215_SR_666KHZ:
        case AT86RF215_SR_500KHZ:
        case AT86RF215_SR_400KHZ:
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }

    switch (rcut) {
        case AT86RF215_RCUT_25FS2:
        case AT86RF215_RCUT_37FS2:
        case AT86RF215_RCUT_50FS2:
        case AT86RF215_RCUT_75FS2:
        case AT86RF215_RCUT_100FS2:
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }

    const uint8_t val = (rcut << 5) | ((dm & 0x1) << 4) | sr;
    return at86rf215_reg_write_8(h, val, reg);
}

/**
 * Configures the RX digital frontend parameters using the RXDFE register.
 * @param h the device handle
 * @param radio the RF frontend
 * @param rcut RX filter relative to the cut-off frequency
 * @param sr the sampling rate setting
 * @return 0 on success or negative error code
 */
static int set_rxdfe(struct at86rf215 *h, at86rf215_radio_t radio, at86rf215_rcut_t rcut, at86rf215_sr_t sr) {
    uint16_t reg = 0x0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_RXDFE;
    } else {
        reg = REG_RF24_RXDFE;
    }
    switch (sr) {
        case AT86RF215_SR_4000KHZ:
        case AT86RF215_SR_2000KHZ:
        case AT86RF215_SR_1333KHZ:
        case AT86RF215_SR_1000KHZ:
        case AT86RF215_SR_800KHZ:
        case AT86RF215_SR_666KHZ:
        case AT86RF215_SR_500KHZ:
        case AT86RF215_SR_400KHZ:
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }

    switch (rcut) {
        case AT86RF215_RCUT_25FS2:
        case AT86RF215_RCUT_37FS2:
        case AT86RF215_RCUT_50FS2:
        case AT86RF215_RCUT_75FS2:
        case AT86RF215_RCUT_100FS2:
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }

    const uint8_t val = (rcut << 5) | sr;
    return at86rf215_reg_write_8(h, val, reg);
}
/**
 * Configures the RX digital frontend cuttoff frequency using the RXDFE
 * register. The rest of the contents of the RXDFE register remain unaffected
 * and are configured via the at86rf215_bb_conf() function.
 * @param h the device handle
 * @param radio the RF frontend
 * @param rcut RX filter relative to the cut-off frequency
 * @return 0 on success or negative error code
 */
int at86rf215_set_rx_rcut(struct at86rf215 *h, at86rf215_radio_t radio, uint8_t rcut) {
    int ret = supports_rf(h, radio);
    if (ret) {
        return ret;
    }
    uint16_t reg = 0x0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_RXDFE;
    } else {
        reg = REG_RF24_RXDFE;
    }
    uint8_t val = 0;
    ret = at86rf215_reg_read_8(h, &val, reg);
    if (ret) {
        return ret;
    }
    val &= 0x1F;
    return at86rf215_reg_write_8(h, ((rcut & 0x7) << 5) | val, reg);
}

/**
 * Controls the PA power
 * @param h the device handle
 * @param radio the RF frontend
 * @param pacur PA power control
 * @param power the output PA (0-31). 1-dB step resolution
 * @return 0 on success or negative error code
 */
int at86rf215_set_pac(struct at86rf215 *h, at86rf215_radio_t radio, at86rf215_pacur_t pacur, uint8_t power) {
    int ret = supports_rf(h, radio);
    if (ret) {
        return ret;
    }
    switch (pacur) {
        case AT86RF215_PACUR_22mA_RED:
        case AT86RF215_PACUR_18mA_RED:
        case AT86RF215_PACUR_11mA_RED:
        case AT86RF215_PACUR_NO_RED:
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    if (power > 31) {
        power = 31;
    }

    uint16_t reg = 0x0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_PAC;
    } else {
        reg = REG_RF24_PAC;
    }
    return at86rf215_reg_write_8(h, (pacur << 5) | power, reg);
}

/**
 * Set the channel of the RF frontend
 * @note the RF frontend should be in IEEE compliant channel mode. Otherwise the
 * -AT86RF215_INVAL_MODE error code is returned
 * @param h the device handle
 * @param radio the radio frontend
 * @param channel channel index
 * @return 0 on success or negative error code
 */
int at86rf215_set_channel(struct at86rf215 *h, at86rf215_radio_t radio, uint16_t channel) {
    int ret = radio_ready(h, radio);
    if (ret) {
        return ret;
    }
    if (channel > 0x1F) {
        return -AT86RF215_INVAL_PARAM;
    }
    /* The radio should be in IEEE channel mode to set a channel */
    if (h->priv.radios[radio].cm != AT86RF215_CM_IEEE) {
        return -AT86RF215_INVAL_CONF;
    }
    if (radio == AT86RF215_RF09) {
        ret = at86rf215_reg_write_8(h, channel & 0xFF, REG_RF09_CNL);
        if (ret) {
            return ret;
        }
        ret = at86rf215_reg_write_8(h, (h->priv.radios[radio].cm << 6) | ((channel >> 8) & 0x1), REG_RF09_CNM);
        if (ret) {
            return ret;
        }
    } else {
        ret = at86rf215_reg_write_8(h, channel & 0xFF, REG_RF24_CNL);
        if (ret) {
            return ret;
        }
        ret = at86rf215_reg_write_8(h, (h->priv.radios[radio].cm << 6) | ((channel >> 8) & 0x1), REG_RF24_CNM);
        if (ret) {
            return ret;
        }
    }
    return AT86RF215_OK;
}

/**
 * Set the center frequency of the RF frontend.
 * @note the RF frontend should be in Fine Resolution mode. Otherwise the
 * -AT86RF215_INVAL_MODE error code is returned
 *
 * @param h the device handle
 * @param radio the RF frontend
 * @param freq the center frequency in Hz
 * @return 0 on success or negative error code
 */
int at86rf215_set_freq(struct at86rf215 *h, at86rf215_radio_t radio, uint32_t freq) {
    int ret = radio_ready(h, radio);
    if (ret) {
        return ret;
    }
    /* The radio should be in fine freq mode to set the frequency */
    if (radio == AT86RF215_RF09) {
        uint32_t x = 0;
        if (h->priv.radios[AT86RF215_RF09].cm == AT86RF215_CM_FINE_RES_04) {
            if (freq < 389500000 || freq > 510000000) {
                return -AT86RF215_INVAL_PARAM;
            }
            x = ((freq - 377e6) * (1 << 16)) / 6.5e6;
        } else if (h->priv.radios[AT86RF215_RF09].cm == AT86RF215_CM_FINE_RES_09) {
            if (freq < 779000000 || freq > 1020000000) {
                return -AT86RF215_INVAL_PARAM;
            }
            x = ((freq - 754e6) * (1 << 16)) / 13e6;
        } else {
            return -AT86RF215_INVAL_CONF;
        }
        /* Apply the frequency setting */
        ret = at86rf215_reg_write_8(h, (x >> 16) & 0xFF, REG_RF09_CCF0H);
        if (ret) {
            return ret;
        }
        ret = at86rf215_reg_write_8(h, (x >> 8) & 0xFF, REG_RF09_CCF0L);
        if (ret) {
            return ret;
        }
        ret = at86rf215_reg_write_8(h, x & 0xFF, REG_RF09_CNL);
        if (ret) {
            return ret;
        }
        ret = at86rf215_reg_write_8(h, h->priv.radios[radio].cm << 6, REG_RF09_CNM);
        if (ret) {
            return ret;
        }
    } else {
        uint32_t x = 0;
        if (h->priv.radios[AT86RF215_RF24].cm == AT86RF215_CM_FINE_RES_24) {
            if (freq < 2400000000 || freq > 2486000000) {
                return -AT86RF215_INVAL_PARAM;
            }
            x = ((freq - 2366e6) * (1 << 16)) / 26e6;
        } else {
            return -AT86RF215_INVAL_CONF;
        }
        /* Apply the frequency setting */
        ret = at86rf215_reg_write_8(h, (x >> 16) & 0xFF, REG_RF24_CCF0H);
        if (ret) {
            return ret;
        }
        ret = at86rf215_reg_write_8(h, (x >> 8) & 0xFF, REG_RF24_CCF0L);
        if (ret) {
            return ret;
        }
        ret = at86rf215_reg_write_8(h, x & 0xFF, REG_RF24_CNL);
        if (ret) {
            return ret;
        }
        ret = at86rf215_reg_write_8(h, h->priv.radios[radio].cm << 6, REG_RF24_CNM);
        if (ret) {
            return ret;
        }
    }
    return AT86RF215_OK;
}

/**
 * Enable/disable the AT86RF215 IRQ line
 * @note Especially for bare metal applications (no-OS), race conditions may
 * occur, while a write SPI transaction activates an IRQ. The IRQ handler will
 * initiate an SPI read transaction before the end of the SPI write. Users may
 * override the default implementation of this function according to their
 * execution environment.
 * @param h the device handle
 * @param enable 1 to enable, 0 to disable
 * @return 0 on success or negative error code
 */
__attribute__((weak)) int at86rf215_irq_enable(struct at86rf215 *h, uint8_t enable) {
    (void)enable;
    return AT86RF215_OK;
}

/**
 * Get the PLL lock status for the corresponding RF frontend
 * @param h the device handle
 * @param status pointer to store the lock status result
 * @param radio the RF frontend
 * @return 0 on success or negative error code
 */
int at86rf215_get_pll_ls(struct at86rf215 *h, at86rf215_pll_ls_t *status, at86rf215_radio_t radio) {
    int ret = radio_ready(h, radio);
    if (ret) {
        return ret;
    }
    uint16_t reg = 0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_PLL;
    } else {
        reg = REG_RF24_PLL;
    }
    uint8_t val = 0;
    ret = at86rf215_reg_read_8(h, &val, reg);
    if (ret) {
        return ret;
    }
    *status = (at86rf215_pll_ls_t)((val >> 1) & 0x1);
    return AT86RF215_OK;
}

/**
 * @brief Sets the RX bandwidth configuration
 *
 * @param h the device handle
 * @param radio the RF frontend
 * @param if_inv if 1, the IF will be inverted
 * @param if_shift if 1, the IF will be shifted at 2500
 * @param bw the RX bandwidth setting
 * @return 0 on success or negative error code
 */
int at86rf215_set_bw(struct at86rf215 *h, at86rf215_radio_t radio, uint8_t if_inv, uint8_t if_shift, at86rf215_rx_bw_t bw) {
    int ret = radio_ready(h, radio);
    if (ret) {
        return ret;
    }

    switch (bw) {
        case AT86RF215_RF_BW160KHZ_IF250KHZ:
        case AT86RF215_RF_BW200KHZ_IF250KHZ:
        case AT86RF215_RF_BW250KHZ_IF250KHZ:
        case AT86RF215_RF_BW320KHZ_IF500KHZ:
        case AT86RF215_RF_BW400KHZ_IF500KHZ:
        case AT86RF215_RF_BW500KHZ_IF500KHZ:
        case AT86RF215_RF_BW630KHZ_IF1000KHZ:
        case AT86RF215_RF_BW800KHZ_IF1000KHZ:
        case AT86RF215_RF_BW1000KHZ_IF1000KHZ:
        case AT86RF215_RF_BW1250KHZ_IF2000KHZ:
        case AT86RF215_RF_BW1600KHZ_IF2000KHZ:
        case AT86RF215_RF_BW2000KHZ_IF2000KHZ:
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }

    uint16_t reg = 0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_RXBWC;
    } else {
        reg = REG_RF24_RXBWC;
    }
    uint8_t val = ((if_inv & 0x1) << 5) | ((if_shift & 0x1) << 4) | bw;
    return at86rf215_reg_write_8(h, val, reg);
}

/**
 * @brief Gets the RSSI of the RF frontend
 *
 * @param h the device handle
 * @param radio the RF frontend
 * @param rssi pointer to store the result
 * @return 0 on success, -AT86RF215_INVAL_VAL in case of invalid RSSI
 * or other appropriate negative error code
 */
int at86rf215_get_rssi(struct at86rf215 *h, at86rf215_radio_t radio, float *rssi) {
    int ret = radio_ready(h, radio);
    if (ret) {
        return ret;
    }

    if (!rssi) {
        return -AT86RF215_INVAL_PARAM;
    }
    uint16_t reg = 0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_RSSI;
    } else {
        reg = REG_RF24_RSSI;
    }
    uint8_t val = 0;
    ret = at86rf215_reg_read_8(h, &val, reg);
    if (ret) {
        return ret;
    }
    if ((int8_t)val == 127) {
        return -AT86RF215_INVAL_VAL;
    }
    *rssi = (int8_t)val;
    return AT86RF215_OK;
}

int at86rf215_get_edv(struct at86rf215 *h, at86rf215_radio_t radio, float *edv) {
    int ret = radio_ready(h, radio);
    if (ret) {
        return ret;
    }

    if (!edv) {
        return -AT86RF215_INVAL_PARAM;
    }
    uint16_t reg = 0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_EDV;
    } else {
        reg = REG_RF24_EDV;
    }
    uint8_t val = 0;
    ret = at86rf215_reg_read_8(h, &val, reg);
    if (ret) {
        return ret;
    }
    if ((int8_t)val == 127) {
        return -AT86RF215_INVAL_VAL;
    }
    *edv = (int8_t)val;
    return AT86RF215_OK;
}

int at86rf215_set_agc(struct at86rf215 *h, at86rf215_radio_t radio, const struct at86rf215_agc_conf *cnf) {
    int ret = radio_ready(h, radio);
    if (ret) {
        return ret;
    }
    if (!cnf) {
        return -AT86RF215_INVAL_PARAM;
    }

    /* If AGC is enabled, user may pass an invalid value */
    uint8_t gcw = cnf->gcw;
    if (cnf->gcw > 23) {
        gcw = 23;
    }

    uint16_t reg = 0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_AGCC;
    } else {
        reg = REG_RF24_AGCC;
    }

    uint8_t val = (cnf->input << 6) | (cnf->avgs << 4) | (cnf->reset << 3) | (cnf->freeze << 1) | cnf->enable;
    ret = at86rf215_reg_write_8(h, val, reg);
    if (ret) {
        return ret;
    }

    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_AGCS;
    } else {
        reg = REG_RF24_AGCS;
    }
    val = (cnf->tgt << 5) | gcw;
    ret = at86rf215_reg_write_8(h, val, reg);
    if (ret) {
        return ret;
    }
    return AT86RF215_OK;
}

/**
 * @brief Sets the AGC target relatively to the ADC full scale
 *
 * @param h the device handle
 * @param radio the RF frontend
 * @param tgt the AGC target
 * @return 0 on success or an appropriate negative error code
 */
int at86rf215_set_agc_target(struct at86rf215 *h, at86rf215_radio_t radio, at86rf215_agc_tgt_t tgt) {
    int ret = radio_ready(h, radio);
    if (ret) {
        return ret;
    }
    switch (tgt) {
        case AT86RF215_TGT_M21:
        case AT86RF215_TGT_M24:
        case AT86RF215_TGT_M27:
        case AT86RF215_TGT_M30:
        case AT86RF215_TGT_M33:
        case AT86RF215_TGT_M36:
        case AT86RF215_TGT_M39:
        case AT86RF215_TGT_M42:
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }

    uint16_t reg = 0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_AGCS;
    } else {
        reg = REG_RF24_AGCS;
    }
    uint8_t val = 0;
    ret = at86rf215_reg_read_8(h, &val, reg);
    if (ret) {
        return ret;
    }

    val &= 0x1F;
    val |= (tgt << 5);
    ret = at86rf215_reg_write_8(h, val, reg);
    if (ret) {
        return ret;
    }
    return AT86RF215_OK;
}

int at86rf215_set_agc_control(struct at86rf215 *h, at86rf215_radio_t radio, uint8_t freeze, uint8_t en) {
    int ret = radio_ready(h, radio);
    if (ret) {
        return ret;
    }
    uint16_t reg = 0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_AGCC;
    } else {
        reg = REG_RF24_AGCC;
    }
    uint8_t val = 0;
    ret = at86rf215_reg_read_8(h, &val, reg);
    if (ret) {
        return ret;
    }
    val &= 0xFC;

    val |= ((freeze & 0x1) << 1) | (en & 0x1);
    ret = at86rf215_reg_write_8(h, val, reg);
    if (ret) {
        return ret;
    }
    return AT86RF215_OK;
}

int at86rf215_set_agc_gain(struct at86rf215 *h, at86rf215_radio_t radio, uint8_t gain) {
    int ret = radio_ready(h, radio);
    if (ret) {
        return ret;
    }

    if (!gain) {
        return -AT86RF215_INVAL_PARAM;
    }
    uint16_t reg = 0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_AGCS;
    } else {
        reg = REG_RF24_AGCS;
    }
    uint8_t val = 0;
    ret = at86rf215_reg_read_8(h, &val, reg);
    if (ret) {
        return ret;
    }
    val &= 0xE0;
    val |= (gain & 0x1F);
    // FIXME
    val |= 0x3 << 4;
    ret = at86rf215_reg_write_8(h, val, reg);
    if (ret) {
        return ret;
    }
    return AT86RF215_OK;
}

/**
 * @brief Gets the current AGC gain value
 *
 * @param h the device handle
 * @param radio the RF frontend
 * @param gain pointer to store the result
 * @return 0 on success or an appropriate negative error code
 */
int at86rf215_get_agc_gain(struct at86rf215 *h, at86rf215_radio_t radio, uint8_t *gain) {
    int ret = radio_ready(h, radio);
    if (ret) {
        return ret;
    }

    if (!gain) {
        return -AT86RF215_INVAL_PARAM;
    }
    uint16_t reg = 0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_AGCS;
    } else {
        reg = REG_RF24_AGCS;
    }
    uint8_t val = 0;
    ret = at86rf215_reg_read_8(h, &val, reg);
    if (ret) {
        return ret;
    }
    *gain = val & 0x1F;
    return AT86RF215_OK;
}

int at86rf215_set_aux_settings(struct at86rf215 *h, at86rf215_radio_t radio, const struct at86rf215_aux_conf *cnf) {
    int ret = radio_ready(h, radio);
    if (ret) {
        return ret;
    }
    if (!cnf) {
        return -AT86RF215_INVAL_PARAM;
    }

    switch (cnf->agcmap) {
        case AT86RF215_AGC_MAP_INTERNAL:
        case AT86RF215_AGC_MAP_9DB:
        case AT86RF215_AGC_MAP_12DB:
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }

    switch (cnf->pavc) {
        case AT86RF215_PAVC_2_0V:
        case AT86RF215_PAVC_2_2V:
        case AT86RF215_PAVC_2_4V:
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
            ;
    }

    uint16_t reg = 0;
    if (radio == AT86RF215_RF09) {
        reg = REG_RF09_AUXS;
    } else {
        reg = REG_RF24_AUXS;
    }

    uint8_t val = (cnf->extlnabyp << 7) | (cnf->agcmap << 5) | (cnf->avext << 4) | (cnf->aven << 3) | cnf->pavc;
    ret = at86rf215_reg_write_8(h, val, reg);
    if (ret) {
        return ret;
    }
    return AT86RF215_OK;
}

static void handle_bb_irq(struct at86rf215 *h, at86rf215_radio_t radio, uint8_t bbcn_irqs) {
    struct at86rf215_radio *r = &h->priv.radios[radio];
    if (bbcn_irqs & BIT(4)) {
        r->tx_complete = 1;
    }
}

static void handle_rf_irq(struct at86rf215 *h, at86rf215_radio_t radio, uint8_t rfn_irqs) {
    struct at86rf215_radio *r = &h->priv.radios[radio];
    if (rfn_irqs & BIT(1)) {
        r->trxready = 1;
    }
}

/**
 * The IRQ handler of the AT86RF215. All IRQ sources are automatically
 * acknowledged
 * @note If custom IRQ handling is needed, please re-implement the
 * at86rf215_irq_user_callback() which is called internally by this handler.
 * @param h the device handle
 * @return 0 on success or negative error code
 */
int at86rf215_irq_callback(struct at86rf215 *h) {
    int ret = ready(h);
    if (ret) {
        return ret;
    }

    /*
     * Read and acknowledge all IRQ sources
     * NOTE: Block mode did not acknowledged the triggered IRQs, even if
     * the manual says that it should
     */
    uint8_t irqs[4] = {0x0, 0x0, 0x0, 0x0};
    at86rf215_reg_read_8(h, &irqs[0], REG_RF09_IRQS);
    at86rf215_reg_read_8(h, &irqs[1], REG_RF24_IRQS);
    at86rf215_reg_read_8(h, &irqs[2], REG_BBC0_IRQS);
    at86rf215_reg_read_8(h, &irqs[3], REG_BBC1_IRQS);

    handle_rf_irq(h, AT86RF215_RF09, irqs[0]);
    handle_rf_irq(h, AT86RF215_RF24, irqs[1]);
    handle_bb_irq(h, AT86RF215_RF09, irqs[2]);
    handle_bb_irq(h, AT86RF215_RF24, irqs[3]);

    return at86rf215_irq_user_callback(h, irqs[0], irqs[1], irqs[2], irqs[3]);
}

/**
 * @brief Clears all pending IRQs
 *
 * @param h the device handle
 * @return 0 on success or negative error code
 */
int at86rf215_irq_clear(struct at86rf215 *h) {
    int ret = ready(h);
    if (ret) {
        return ret;
    }

    /*
     * Read and acknowledge all IRQ sources
     * NOTE: Block mode did not acknowledged the triggered IRQs, even if
     * the manual says that it should
     */
    uint8_t irqs;
    at86rf215_reg_read_8(h, &irqs, REG_RF09_IRQS);
    at86rf215_reg_read_8(h, &irqs, REG_RF24_IRQS);
    at86rf215_reg_read_8(h, &irqs, REG_BBC0_IRQS);
    at86rf215_reg_read_8(h, &irqs, REG_BBC1_IRQS);
    return AT86RF215_OK;
}

/**
 * @brief Clears all pending IRQs of a specific radio
 *
 * @param h the device handle
 * @param radio the RF frontend
 * @return 0 on success or negative error code
 */
int at86rf215_radio_irq_clear(struct at86rf215 *h, at86rf215_radio_t radio) {
    int ret = ready(h);
    if (ret) {
        return ret;
    }

    /*
     * Read and acknowledge all IRQ sources
     * NOTE: Block mode did not acknowledged the triggered IRQs, even if
     * the manual says that it should
     */
    uint8_t irqs;
    switch (radio) {
        case AT86RF215_RF09:
            at86rf215_reg_read_8(h, &irqs, REG_RF09_IRQS);
            at86rf215_reg_read_8(h, &irqs, REG_BBC0_IRQS);
            break;
        case AT86RF215_RF24:
            at86rf215_reg_read_8(h, &irqs, REG_RF24_IRQS);
            at86rf215_reg_read_8(h, &irqs, REG_BBC1_IRQS);
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    return AT86RF215_OK;
}

/**
 * Custom IRQ handling. Called internally by the at86rf215_irq_callback()
 * @param h the device handle
 * @param rf09_irqs RF09_IRQS register
 * @param rf24_irqs RF24_IRQS register
 * @param bbc0_irqs BBC0_IRQS register
 * @param bbc1_irqs BBC1_IRQS register
 * @return 0 on success or negative error code
 */
__attribute__((weak)) int at86rf215_irq_user_callback(struct at86rf215 *h, uint8_t rf09_irqs, uint8_t rf24_irqs, uint8_t bbc0_irqs,
                                                      uint8_t bbc1_irqs) {
    return AT86RF215_OK;
}

static int bb_conf_mrfsk(struct at86rf215 *h, at86rf215_radio_t radio, const struct at86rf215_bb_conf *conf) {
    /*
     * In order to process efficiently the configuration registers for
     * the two available baseband cores, we use the constant offset of the
     * configuration address space of the two cores. For some RF related
     * configuration registers (e.g. TXDFE, RXDFE) the offset between the
     * two address spaces for the radio configuration is exactly the same.
     */
    uint16_t offset;
    switch (radio) {
        case AT86RF215_RF09:
            offset = 0;
            break;
        case AT86RF215_RF24:
            offset = 256;
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }

    at86rf215_sr_t tx_sr = AT86RF215_SR_4000KHZ;
    at86rf215_sr_t rx_sr = AT86RF215_SR_4000KHZ;
    switch (conf->fsk.srate) {
        case AT86RF215_FSK_SRATE_50:
            if (h->priv.version == 1) {
                tx_sr = AT86RF215_SR_400KHZ;
            } else {
                tx_sr = AT86RF215_SR_500KHZ;
            }
            rx_sr = AT86RF215_SR_400KHZ;
            break;
        case AT86RF215_FSK_SRATE_100:
            if (h->priv.version == 1) {
                tx_sr = AT86RF215_SR_800KHZ;
            } else {
                tx_sr = AT86RF215_SR_1000KHZ;
            }
            rx_sr = AT86RF215_SR_800KHZ;
            break;
        case AT86RF215_FSK_SRATE_150:
            tx_sr = AT86RF215_SR_2000KHZ;
            rx_sr = AT86RF215_SR_1000KHZ;
            break;
        case AT86RF215_FSK_SRATE_200:
            tx_sr = AT86RF215_SR_2000KHZ;
            rx_sr = AT86RF215_SR_1000KHZ;
            break;
        case AT86RF215_FSK_SRATE_300:
            tx_sr = AT86RF215_SR_4000KHZ;
            rx_sr = AT86RF215_SR_2000KHZ;
            break;
        case AT86RF215_FSK_SRATE_400:
            tx_sr = AT86RF215_SR_4000KHZ;
            rx_sr = AT86RF215_SR_2000KHZ;
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }

    at86rf215_rcut_t rx_rcut = AT86RF215_RCUT_25FS2;
    uint8_t ifs = 0;
    at86rf215_rx_bw_t rbw = AT86RF215_RF_BW160KHZ_IF250KHZ;
    if (conf->fsk.midx >= AT86RF215_MIDX_3) {
        if (radio == AT86RF215_RF09) {
            rx_rcut = fsk_rx_conf_rcut_midx3_09[conf->fsk.srate];
            rbw = fsk_rx_conf_rbw_midx3_09[conf->fsk.srate];
            ifs = fsk_rx_conf_ifs_midx3_09[conf->fsk.srate];
        } else {
            rx_rcut = fsk_rx_conf_rcut_midx3_24[conf->fsk.srate];
            rbw = fsk_rx_conf_rbw_midx3_24[conf->fsk.srate];
            ifs = fsk_rx_conf_ifs_midx3_24[conf->fsk.srate];
        }
    } else {
        if (radio == AT86RF215_RF09) {
            rx_rcut = fsk_rx_conf_rcut_midx1_09[conf->fsk.srate];
            rbw = fsk_rx_conf_rbw_midx1_09[conf->fsk.srate];
            ifs = fsk_rx_conf_ifs_midx1_09[conf->fsk.srate];
        } else {
            rx_rcut = fsk_rx_conf_rcut_midx1_24[conf->fsk.srate];
            rbw = fsk_rx_conf_rbw_midx1_24[conf->fsk.srate];
            ifs = fsk_rx_conf_ifs_midx1_24[conf->fsk.srate];
        }
    }

    at86rf215_rcut_t tx_rcut = AT86RF215_RCUT_25FS2;
    if (conf->fsk.midx >= AT86RF215_MIDX_3) {
        tx_rcut = fsk_tx_conf_rcut_midx3[conf->fsk.srate];
    } else {
        tx_rcut = fsk_tx_conf_rcut_midx1[conf->fsk.srate];
    }

    uint8_t val;
    /* FSKC0 */
    switch (conf->fsk.mord) {
        case AT86RF215_2FSK:
            break;
        case AT86RF215_4FSK:
            /* Check for 4FSK restrictions (h >= 1, BT = 2) */
            if (conf->fsk.bt != 2 || conf->fsk.midx < AT86RF215_MIDX_3) {
                return -AT86RF215_INVAL_CONF;
            }
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    val = conf->fsk.mord;
    switch (conf->fsk.midx) {
        case AT86RF215_MIDX_0:
        case AT86RF215_MIDX_1:
        case AT86RF215_MIDX_2:
        case AT86RF215_MIDX_3:
        case AT86RF215_MIDX_4:
        case AT86RF215_MIDX_5:
        case AT86RF215_MIDX_6:
        case AT86RF215_MIDX_7:
            val |= conf->fsk.midx << 1;
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }

    switch (conf->fsk.midxs) {
        case AT86RF215_MIDXS_78:
        case AT86RF215_MIDXS_88:
        case AT86RF215_MIDXS_98:
        case AT86RF215_MIDXS_108:
            val |= conf->fsk.midxs << 4;
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    switch (conf->fsk.bt) {
        case AT86RF215_FSK_BT_05:
        case AT86RF215_FSK_BT_10:
        case AT86RF215_FSK_BT_15:
        case AT86RF215_FSK_BT_20:
            val |= conf->fsk.bt << 6;
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    int ret = at86rf215_reg_write_8(h, val, REG_BBC0_FSKC0 + offset);
    if (ret) {
        return ret;
    }

    /* FSKC1 */
    val = conf->fsk.srate | (conf->fsk.fi << 5) | ((conf->fsk.preamble_length >> 2) & 0xC0);
    ret = at86rf215_reg_write_8(h, val, REG_BBC0_FSKC1 + offset);
    if (ret) {
        return ret;
    }

    /* FSKC2 */
    val = conf->fsk.fecie;
    switch (conf->fsk.fecs) {
        case AT86RF215_FSK_FEC_NRNSC:
        case AT86RF215_FSK_FEC_RSC:
            val |= conf->fsk.fecs << 1;
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    val |= (conf->fsk.pri << 2) | (conf->fsk.mse << 3) | (conf->fsk.rxpto << 4);
    switch (conf->fsk.rxo) {
        case AT86RF215_FSK_RXO_6DB:
        case AT86RF215_FSK_RXO_12DB:
        case AT86RF215_FSK_RXO_18DB:
        case AT86RF215_FSK_RXO_DISABLED:
            val |= conf->fsk.rxo << 5;
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    val |= conf->fsk.pdtm << 7;
    ret = at86rf215_reg_write_8(h, val, REG_BBC0_FSKC2 + offset);
    if (ret) {
        return ret;
    }

    /* FSKC3 */
    val = (conf->fsk.sfd_threshold << 4) | conf->fsk.preamble_threshold;
    ret = at86rf215_reg_write_8(h, val, REG_BBC0_FSKC3 + offset);
    if (ret) {
        return ret;
    }

    /* FSKC4 */
    switch (conf->fsk.csfd0) {
        case AT86RF215_SFD_UNCODED_IEEE:
        case AT86RF215_SFD_UNCODED_RAW:
        case AT86RF215_SFD_CODED_IEEE:
        case AT86RF215_SFD_CODED_RAW:
            val = conf->fsk.csfd0;
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    switch (conf->fsk.csfd1) {
        case AT86RF215_SFD_UNCODED_IEEE:
        case AT86RF215_SFD_UNCODED_RAW:
        case AT86RF215_SFD_CODED_IEEE:
        case AT86RF215_SFD_CODED_RAW:
            val |= conf->fsk.csfd1 << 2;
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    val |= (conf->fsk.rawrbit << 4) | (conf->fsk.sfd32 << 5) | (conf->fsk.sfdq << 6);
    ret = at86rf215_reg_write_8(h, val, REG_BBC0_FSKC4 + offset);
    if (ret) {
        return ret;
    }

    /* FSKPLL */
    val = conf->fsk.preamble_length;
    ret = at86rf215_reg_write_8(h, val, REG_BBC0_FSKPLL + offset);
    if (ret) {
        return ret;
    }

    /* SFD configuration */
    ret = at86rf215_reg_write_8(h, conf->fsk.sfd0, REG_BBC0_FSKSFD0L + offset);
    if (ret) {
        return ret;
    }
    ret = at86rf215_reg_write_8(h, conf->fsk.sfd0 >> 8, REG_BBC0_FSKSFD0H + offset);
    if (ret) {
        return ret;
    }
    ret = at86rf215_reg_write_8(h, conf->fsk.sfd1, REG_BBC0_FSKSFD1L + offset);
    if (ret) {
        return ret;
    }
    ret = at86rf215_reg_write_8(h, conf->fsk.sfd1 >> 8, REG_BBC0_FSKSFD1H + offset);
    if (ret) {
        return ret;
    }

    /* FSKPHRTX */
    val = conf->fsk.rb1 | (conf->fsk.rb2 << 1) | (conf->fsk.dw << 2) | (conf->fsk.sfd << 3);
    ret = at86rf215_reg_write_8(h, val, REG_BBC0_FSKPHRTX + offset);
    if (ret) {
        return ret;
    }

    /*
     * FSKDM
     * NOTE: Both TXDFE and FSK DM should have the direct modulation option
     * enabled
     */
    ret = at86rf215_reg_write_8(h, conf->fsk.dm | (conf->fsk.preemphasis << 1), REG_BBC0_FSKDM + offset);
    if (ret) {
        return ret;
    }

    /* PRemphasis filter setup */
    ret = at86rf215_reg_write_8(h, conf->fsk.preemphasis_taps, REG_BBC0_FSKPE0 + offset);
    if (ret) {
        return ret;
    }

    ret = at86rf215_reg_write_8(h, conf->fsk.preemphasis_taps >> 8, REG_BBC0_FSKPE1 + offset);
    if (ret) {
        return ret;
    }

    ret = at86rf215_reg_write_8(h, conf->fsk.preemphasis_taps >> 16, REG_BBC0_FSKPE2 + offset);
    if (ret) {
        return ret;
    }

    /* Apply TX sampling rate and cutoff frequency */
    ret = set_txdfe(h, radio, tx_rcut, conf->fsk.dm, tx_sr);
    if (ret) {
        return ret;
    }

    /* Apply RX sampling rate and cutoff frequency */
    ret = set_rxdfe(h, radio, rx_rcut, rx_sr);
    if (ret) {
        return ret;
    }

    /* Apply RX filtering */
    ret = at86rf215_set_bw(h, radio, 0, ifs, rbw);
    if (ret) {
        return ret;
    }

    /* Apply FSK frame length for RAW mode */
    val = conf->fsk.fskrrxf >> 8;
    ret = at86rf215_reg_write_8(h, val, REG_BBC0_FSKRRXFLH + offset);
    if (ret) {
        return ret;
    }
    val = conf->fsk.fskrrxf;
    ret = at86rf215_reg_write_8(h, val, REG_BBC0_FSKRRXFLL + offset);
    if (ret) {
        return ret;
    }

    return AT86RF215_OK;
}

/**
 * @note The baseband core is explicitly disabled with the call of this function
 * (PC.BBEN = 0). Use the at86rf215_bb_enable() to enable it.
 * @param h the device handle
 * @param radio the RF frontend
 * @param conf the condiguration of the baseband core
 * @return 0 on success or negative error code
 */
int at86rf215_bb_conf(struct at86rf215 *h, at86rf215_radio_t radio, const struct at86rf215_bb_conf *conf) {
    int ret = supports_rf(h, radio);
    if (ret) {
        return ret;
    }
    if (!conf) {
        return -AT86RF215_INVAL_PARAM;
    }
    uint8_t val = conf->pt | (conf->fcst << 3) | (conf->txafcs << 4) | (conf->fcsfe << 6) | (conf->ctx << 3);

    uint16_t reg = 0;
    if (radio == AT86RF215_RF09) {
        reg = REG_BBC0_PC;
    } else {
        reg = REG_BBC1_PC;
    }
    ret = at86rf215_reg_write_8(h, val, reg);
    if (ret) {
        return ret;
    }

    switch (conf->pt) {
        case AT86RF215_BB_MRFSK:
            ret = bb_conf_mrfsk(h, radio, conf);
            break;
        case AT86RF215_BB_MROFDM:
        case AT86RF215_BB_MROQPSK:
        case AT86RF215_BB_PHYOFF:
            return -AT86RF215_NOT_IMPL;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    if (ret) {
        return ret;
    }
    /* Copy internally the configuration */
    memcpy(&h->priv.bbc[radio], conf, sizeof(struct at86rf215_bb_conf));
    return AT86RF215_OK;
}

/**
 * Enables the baseband core of the corresponding radio frontend
 * @param h the device handle
 * @param radio the RF frontend
 * @param en 1 to enable, 0 to disable
 * @return 0 on success or negative error code
 */
int at86rf215_bb_enable(struct at86rf215 *h, at86rf215_radio_t radio, uint8_t en) {
    int ret = supports_rf(h, radio);
    if (ret) {
        return ret;
    }
    const struct at86rf215_bb_conf *conf = &h->priv.bbc[radio];
    uint8_t val = conf->pt | ((en & 0x1) << 2) | (conf->fcst << 3) | (conf->txafcs << 4) | (conf->fcsfe << 6) | (conf->ctx << 3);
    uint16_t reg = 0;
    if (radio == AT86RF215_RF09) {
        reg = REG_BBC0_PC;
    } else {
        reg = REG_BBC1_PC;
    }
    ret = at86rf215_reg_write_8(h, val, reg);
    if (ret) {
        return ret;
    }
    return AT86RF215_OK;
}

/**
 * Writes PSDU data to the TX buffer
 * @param h the device handle
 * @param radio the RF frontend
 * @param b buffer with the PSDU data
 * @param len the size of the PSDU
 * @return 0 on success or negative error code
 */
static int write_tx_buffer(struct at86rf215 *h, at86rf215_radio_t radio, const uint8_t *b, size_t len) {
    int ret = 0;
    if (radio == AT86RF215_RF09) {
        /* Declare the size of the PSDU */
        ret = at86rf215_reg_write_8(h, len & 0xFF, REG_BBC0_TXFLL);
        if (ret) {
            return ret;
        }
        ret = at86rf215_reg_write_8(h, (len >> 8) & 0xFF, REG_BBC0_TXFLH);
        if (ret) {
            return ret;
        }

        /* Fill the buffer */
        ret = at86rf215_set_seln(h, 0);
        if (ret) {
            return ret;
        }
        at86rf215_irq_enable(h, 0);
        uint8_t mosi[2] = {(REG_BBC0_FBTXS >> 8) | 0x80, REG_BBC0_FBTXS & 0xFF};
        ret = at86rf215_spi_write(h, mosi, 2);
        if (ret) {
            at86rf215_set_seln(h, 1);
            at86rf215_irq_enable(h, 1);
            return ret;
        }
        ret = at86rf215_spi_write(h, b, len);
        if (ret) {
            at86rf215_set_seln(h, 1);
            at86rf215_irq_enable(h, 1);
            return ret;
        }
    } else {
        /* Declare the size of the PSDU */
        ret = at86rf215_reg_write_8(h, len & 0xFF, REG_BBC1_TXFLL);
        if (ret) {
            return ret;
        }
        ret = at86rf215_reg_write_8(h, (len >> 8) & 0xFF, REG_BBC1_TXFLH);
        if (ret) {
            return ret;
        }

        /* Fill the buffer */
        ret = at86rf215_set_seln(h, 0);
        if (ret) {
            return ret;
        }
        at86rf215_irq_enable(h, 0);
        uint8_t mosi[2] = {(REG_BBC1_FBTXS >> 8) | 0x80, REG_BBC1_FBTXS & 0xFF};
        ret = at86rf215_spi_write(h, mosi, 2);
        if (ret) {
            at86rf215_set_seln(h, 1);
            at86rf215_irq_enable(h, 1);
            return ret;
        }
        ret = at86rf215_spi_write(h, b, len);
        if (ret) {
            at86rf215_set_seln(h, 1);
            at86rf215_irq_enable(h, 1);
            return ret;
        }
    }
    at86rf215_irq_enable(h, 1);
    return at86rf215_set_seln(h, 1);
}

/**
 * @brief Receives a (possibly) received frame from the frame buffer
 *
 * @note This function transfers only the contents of the frame buffer. It does
 * not check for their validity.
 *
 * @param h the device handle
 * @param radio the RF frontend
 * @param psdu buffer to store the received data
 * @param len the number of bytes to read from the frame buffer
 * @return 0 on success or negative error code
 */
int at86rf215_rx_frame(struct at86rf215 *h, at86rf215_radio_t radio, uint8_t *psdu, size_t len) {
    int ret = supports_rf(h, radio);
    if (ret) {
        return ret;
    }
    if (!psdu) {
        return -AT86RF215_INVAL_PARAM;
    }

    ret = at86rf215_set_seln(h, 0);
    if (ret) {
        return ret;
    }
    at86rf215_irq_enable(h, 0);
    /* Construct properly the MOSI buffer */
    const uint16_t reg = radio == AT86RF215_RF09 ? REG_BBC0_FBRXS : REG_BBC1_FBRXS;
    uint8_t mosi[2] = {(reg >> 8) & 0x3F, reg & 0xFF};
    ret = at86rf215_spi_read(h, spi_buffer, mosi, 2, len + 2);
    at86rf215_irq_enable(h, 1);
    at86rf215_set_seln(h, 1);
    memcpy(psdu, spi_buffer + 2, len);
    return ret;
}

/**
 * Transmits a frame using the configured baseband core mode
 * @note the chip mode should ensure that the corresponding baseband core
 * is enabled
 * @param h the device handle
 * @param radio the RF fronted
 * @param psdu the data to send
 * @param len the number of bytes to send
 * @return 0 on success or negative error code
 */
int at86rf215_tx_frame(struct at86rf215 *h, at86rf215_radio_t radio, const uint8_t *psdu, size_t len, size_t timeout_ms) {
    size_t deadline = at86rf215_get_time_ms(h) + timeout_ms;
    int ret = supports_rf(h, radio);
    if (ret) {
        return ret;
    }
    if (!psdu) {
        return -AT86RF215_INVAL_PARAM;
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

    /*
     * Write at least once the TXPREP so we are sure that we can start the
     * transmission
     */
    at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;
    while (state != AT86RF215_STATE_RF_TXPREP) {
        if (at86rf215_get_time_ms(h) > deadline) {
            return -AT86RF215_TIMEOUT;
        }
        at86rf215_set_cmd(h, AT86RF215_CMD_RF_TXPREP, radio);
        at86rf215_delay_us(h, 100);
        at86rf215_get_state(h, &state, radio);
    }

    ret = write_tx_buffer(h, radio, psdu, len);
    if (ret) {
        return ret;
    }

    struct at86rf215_radio *r = &h->priv.radios[radio];
    r->tx_complete = 0;

    /* Data are on the buffer. Issue the TX cmd to send them */
    ret = at86rf215_set_cmd(h, AT86RF215_CMD_RF_TX, radio);
    if (ret) {
        return ret;
    }
    /* Wait for the transfer to complete */
    while (r->tx_complete == 0) {
        if (at86rf215_get_time_ms(h) > deadline) {
            return -AT86RF215_TIMEOUT;
        }
        at86rf215_delay_us(h, 100);
    }
    return AT86RF215_OK;
}

/**
 * @brief Sets the transceiver in RX mode
 *
 * @param h the device handle
 * @param radio the RF fronted
 * @param timeout_ms timeout in milisceconds
 * @return 0 on success or negative error code
 */
int at86rf215_rx(struct at86rf215 *h, at86rf215_radio_t radio, size_t timeout_ms) {
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

/**
 * Configures the IQ mode for a particular RF frontend.
 * @note Some settings are applied for the IQ mode of both sub-1 GHz and the
 * 2.4 GHz RF frontend.
 * @note This function does not change the chip mode in any way. Users should
 * call the at86rf215_set_mode() to chnage the operational mode of the IC
 * @param h the device handle
 * @param radio the RF frontend
 * @param conf IQ configration
 * @return 0 on success or negative error code
 */
int at86rf215_iq_conf(struct at86rf215 *h, at86rf215_radio_t radio, const struct at86rf215_iq_conf *conf) {
    int ret = supports_rf(h, radio);
    if (ret) {
        return ret;
    }
    if (!conf) {
        return -AT86RF215_INVAL_PARAM;
    }

    switch (conf->drv) {
        case AT86RF215_LVDS_DRV1:
        case AT86RF215_LVDS_DRV2:
        case AT86RF215_LVDS_DRV3:
        case AT86RF215_LVDS_DRV4:
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }

    switch (conf->cmv) {
        case AT86RF215_LVDS_CMV150:
        case AT86RF215_LVDS_CMV200:
        case AT86RF215_LVDS_CMV250:
        case AT86RF215_LVDS_CMV300:
            break;
        default:
            return -AT86RF215_INVAL_PARAM;
    }
    uint8_t val = conf->eec | (conf->cmv1v2 << 1) | (conf->cmv << 2) | (conf->drv << 4) | (conf->extlb << 7);
    ret = at86rf215_reg_write_8(h, val, REG_RF_IQIFC0);
    if (ret) {
        return ret;
    }
    /* Set the RF_IQIFC1 but leave the CHPM unchanged  */
    ret = at86rf215_reg_read_8(h, &val, REG_RF_IQIFC1);
    if (ret) {
        return ret;
    }
    val &= BIT(4) | BIT(5) | BIT(6);
    val |= conf->skedrv;
    ret = at86rf215_reg_write_8(h, val, REG_RF_IQIFC1);
    if (ret) {
        return ret;
    }

    /* Apply the TX sampling rate settings */
    ret = set_txdfe(h, radio, conf->trcut, 0, conf->tsr);
    if (ret) {
        return ret;
    }

    /* Apply the RX sampling rate settings */
    ret = set_rxdfe(h, radio, conf->rrcut, conf->rsr);
    if (ret) {
        return ret;
    }
    return AT86RF215_OK;
}
