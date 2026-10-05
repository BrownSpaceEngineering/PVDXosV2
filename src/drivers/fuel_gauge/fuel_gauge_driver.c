/**
 * fuel_gauge_driver.c
 *
 * Bare-metal driver for 4x MAX17205 fuel gauges behind a TCA9546A I2C mux.
 * See fuel_gauge_driver.h for the topology and bus/address overview.
 *
 * Access pattern for a gauge: select its mux channel, then talk to the MAX17205.
 * The MAX17205 has two 7-bit I2C addresses depending on the register page:
 *   reg < 0x100 -> 0x36 (m5 page; also 0x180-0x1FF NV block, reg byte = reg & 0xFF)
 *   reg >= 0x100 -> 0x0B (SBS page; reg byte = reg & 0xFF)
 * 16-bit registers are little-endian (LSB first).
 *
 * The ASF HAL functions (i2c_m_sync_cmd_read/cmd_write/transfer) return 0 on success
 * and a negative value on failure.
 */

#include "globals.h"

#include "fuel_gauge_driver.h"

// Largest downstream payload is a 2-byte register, so no large buffers needed.

// ---------------------- TCA9546A mux ----------------------

// The mux has no internal register: a transaction is just a 1-byte control write (or
// a 1-byte read of the current control byte). Use a raw transfer rather than
// cmd_write, which would prepend a register-address byte.
static int32_t fg_mux_raw_write(uint8_t ctrl) {
    struct _i2c_m_msg msg = {.addr = FG_MUX_ADDR, .flags = I2C_M_STOP, .len = 1, .buffer = &ctrl};
    return i2c_m_sync_transfer(&FG_I2C, &msg);
}

static int32_t fg_mux_raw_read(uint8_t *ctrl) {
    struct _i2c_m_msg msg = {.addr = FG_MUX_ADDR, .flags = I2C_M_STOP | I2C_M_RD, .len = 1, .buffer = ctrl};
    return i2c_m_sync_transfer(&FG_I2C, &msg);
}

/**
 * \brief Opens exactly one mux channel (0-3) and verifies it took.
 * \return SUCCESS, or ERROR_I2C_FAILED / ERROR_SANITY_CHECK_FAILED.
 */
static status_t fg_mux_select(uint8_t channel) {
    if (channel >= FG_NUM_PACKS) {
        return ERROR_BAD_TARGET;
    }

    uint8_t want = FG_MUX_CH(channel);
    if (fg_mux_raw_write(want) != 0) {
        warning("fuel_gauge: mux select write failed (ch=%u)\n", channel);
        return ERROR_I2C_FAILED;
    }

    // Read the control byte back to confirm the channel actually switched.
    uint8_t got = 0xFF;
    if (fg_mux_raw_read(&got) != 0) {
        warning("fuel_gauge: mux control readback failed (ch=%u)\n", channel);
        return ERROR_I2C_FAILED;
    }
    if (got != want) {
        warning("fuel_gauge: mux ch=%u readback 0x%02x != 0x%02x\n", channel, got, want);
        return ERROR_SANITY_CHECK_FAILED;
    }
    return SUCCESS;
}

// Closes all mux channels so the downstream MAX addresses stop shadowing the shared bus.
static void fg_mux_disable(void) {
    if (fg_mux_raw_write(FG_MUX_CH_NONE) != 0) {
        warning("fuel_gauge: mux disable (close all channels) failed\n");
    }
}

// ---------------------- MAX17205 register access ----------------------

// Reads a 16-bit little-endian register from the currently-selected gauge.
static status_t fg_read_reg16(uint16_t reg, uint16_t *out) {
    uint8_t slave = (reg < 0x100) ? FG_MAX_ADDR_LOW : FG_MAX_ADDR_HIGH;
    uint8_t reg_byte = (uint8_t)(reg & 0xFF);
    uint8_t buf[2] = {0};

    i2c_m_sync_set_slaveaddr(&FG_I2C, slave, I2C_M_SEVEN);
    if (i2c_m_sync_cmd_read(&FG_I2C, reg_byte, buf, 2) != 0) {
        warning("fuel_gauge: read reg 0x%03x (slave 0x%02x) failed\n", reg, slave);
        return ERROR_I2C_FAILED;
    }
    *out = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return SUCCESS;
}

// ---------------------- Conversions (see datasheet LSBs) ----------------------

static inline float fg_conv_soc(uint16_t raw) {
    return (float)raw / 256.0f; // RepSOC: 1/256 % per LSB
}

static inline float fg_conv_temp(uint16_t raw) {
    return (float)(int16_t)raw / 256.0f; // Temp: 1/256 degC per LSB, signed
}

static inline float fg_conv_batt(uint16_t raw) {
    return (float)raw * 1.25e-3f; // Batt: 1.25 mV per LSB
}

static inline float fg_conv_current(uint16_t raw) {
    // Current: 1.5625 uV across Rsense per LSB, signed.
    return (float)(int16_t)raw * 1.5625e-6f / FG_RSENSE_OHMS;
}

// ---------------------- Public API ----------------------

/**
 * \brief Brings up the ALRT input and probes all four gauges' configuration.
 *
 * For each channel this logs Status + the volatile/NV cell-config and capacity
 * registers. The PackCfg/nPackCfg NCELLS field reveals whether the part is set up
 * for a 2S pack, which answers the open "is NV programmed?" question by observation.
 * A failure on one gauge is logged and skipped so a single bad gauge does not block
 * the others. No NV is written in this pass.
 */
status_t init_fuel_gauges(void) {
    // Enable the SERCOM6 I2C peripheral. atmel_start_init() only *inits* it (leaves it
    // disabled), so it must be enabled before any transfer (cf. the magnetometer driver's
    // spi_m_sync_enable).
    i2c_m_sync_enable(&FG_I2C);

    // ALRT is a shared active-low open-drain line: input with a pull-up.
    gpio_set_pin_function(EPS_IRQ, GPIO_PIN_FUNCTION_OFF);
    gpio_set_pin_direction(EPS_IRQ, GPIO_DIRECTION_IN);
    gpio_set_pin_pull_mode(EPS_IRQ, GPIO_PULL_UP);

    for (uint8_t ch = 0; ch < FG_NUM_PACKS; ch++) {
        if (fg_mux_select(ch) != SUCCESS) {
            warning("fuel_gauge: gauge %u unreachable (mux select failed)\n", ch + 1);
            continue;
        }

        uint16_t status = 0, packcfg = 0, npackcfg = 0, designcap = 0, ndesigncap = 0;
        status_t rs = SUCCESS, rp = SUCCESS, rn = SUCCESS, rd = SUCCESS, rnd = SUCCESS;

        rs = fg_read_reg16(MAX17205_REG_STATUS, &status);
        rp = fg_read_reg16(MAX17205_REG_PACKCFG, &packcfg);
        rn = fg_read_reg16(MAX17205_REG_NPACKCFG, &npackcfg);
        rd = fg_read_reg16(MAX17205_REG_DESIGNCAP, &designcap);
        rnd = fg_read_reg16(MAX17205_REG_NDESIGNCAP, &ndesigncap);

        if (rs != SUCCESS || rp != SUCCESS || rn != SUCCESS || rd != SUCCESS || rnd != SUCCESS) {
            warning("fuel_gauge: gauge %u config read error(s)\n", ch + 1);
            continue;
        }

        // NCELLS occupies the low 4 bits of PackCfg/nPackCfg (Fig 37). Log raw + NCELLS.
        info("fuel_gauge: gauge %u: Status=0x%04x PackCfg=0x%04x (NCELLS=%u) "
             "nPackCfg=0x%04x (NCELLS=%u) DesignCap=0x%04x nDesignCap=0x%04x\n",
             ch + 1, status, packcfg, (unsigned)(packcfg & 0x000F), npackcfg,
             (unsigned)(npackcfg & 0x000F), designcap, ndesigncap);
    }

    fg_mux_disable();
    return SUCCESS;
}

/**
 * \brief Reads SOC / pack voltage / current / temperature from one gauge.
 * \note Leaves the mux channel open; callers that are done should close it (fg_read_all
 *       does). The caller owns `out`.
 */
status_t fg_read_pack(uint8_t channel, fg_reading_t *out) {
    if (out == NULL) {
        return ERROR_BAD_TARGET;
    }

    ret_err_status(fg_mux_select(channel), "fuel_gauge: mux select failed in read_pack");

    uint16_t soc = 0, batt = 0, current = 0, temp = 0;
    ret_err_status(fg_read_reg16(MAX17205_REG_REPSOC, &soc), "fuel_gauge: RepSOC read failed");
    ret_err_status(fg_read_reg16(MAX17205_REG_BATT, &batt), "fuel_gauge: Batt read failed");
    ret_err_status(fg_read_reg16(MAX17205_REG_CURRENT, &current), "fuel_gauge: Current read failed");
    ret_err_status(fg_read_reg16(MAX17205_REG_TEMP, &temp), "fuel_gauge: Temp read failed");

    out->soc_pct = fg_conv_soc(soc);
    out->pack_voltage = fg_conv_batt(batt);
    out->current_a = fg_conv_current(current);
    out->temp_c = fg_conv_temp(temp);
    return SUCCESS;
}

/**
 * \brief Reads all four packs. A failed pack is logged and its entry left zeroed.
 *        Closes the mux when done.
 */
status_t fg_read_all(fg_reading_t out[FG_NUM_PACKS]) {
    if (out == NULL) {
        return ERROR_BAD_TARGET;
    }

    for (uint8_t ch = 0; ch < FG_NUM_PACKS; ch++) {
        memset(&out[ch], 0, sizeof(out[ch]));
        if (fg_read_pack(ch, &out[ch]) != SUCCESS) {
            warning("fuel_gauge: read of pack %u failed\n", ch + 1);
        }
    }

    fg_mux_disable();
    return SUCCESS;
}

/**
 * \brief Selects a channel, reads one 16-bit register, and closes the mux.
 *        Self-contained probe for arbitrary registers (DevName, per-cell voltages, etc.).
 */
status_t fg_read_raw(uint8_t channel, uint16_t reg, uint16_t *out) {
    if (out == NULL) {
        return ERROR_BAD_TARGET;
    }

    status_t rc = fg_mux_select(channel);
    if (rc != SUCCESS) {
        return rc;
    }
    rc = fg_read_reg16(reg, out);
    fg_mux_disable();
    return rc;
}

// ---------------------- ALRT (polled) ----------------------

// The shared ALRT line is active low: asserted when the pin reads low.
bool fg_alrt_asserted(void) {
    return gpio_get_pin_level(EPS_IRQ) == false;
}

/**
 * \brief When ALRT is asserted, visits each gauge and logs its Status register so the
 *        tripped gauge(s) can be identified. Closes the mux when done.
 */
void fg_scan_alerts(void) {
    for (uint8_t ch = 0; ch < FG_NUM_PACKS; ch++) {
        if (fg_mux_select(ch) != SUCCESS) {
            continue;
        }
        uint16_t status = 0;
        if (fg_read_reg16(MAX17205_REG_STATUS, &status) != SUCCESS) {
            continue;
        }
        info("fuel_gauge: ALRT scan gauge %u Status=0x%04x\n", ch + 1, status);
    }
    fg_mux_disable();
}
