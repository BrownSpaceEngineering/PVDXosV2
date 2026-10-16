/**
 * fuel_gauge_driver.c
 *
 * Bare-metal driver for 4x MAX17205 fuel gauges behind a TCA9546A I2C mux.
 * See fuel_gauge_driver.h for the topology and bus/address overview.
 *
 * Access pattern for a gauge: select its mux channel, then talk to the MAX17205.
 * The MAX17205 has two 7-bit I2C addresses depending on the register page:
 *   reg < 0x100 -> 0x36 (ModelGauge m5 page)
 *   reg >= 0x100 -> 0x0B (SBS/NV page; reg byte = reg & 0xFF)
 * 16-bit registers are little-endian (LSB first).
 *
 * The ASF HAL functions (i2c_m_sync_cmd_read/cmd_write/transfer) return 0 on success
 * and a negative value on failure.
 */

#include "globals.h"

#include "fuel_gauge_driver.h"

// Largest downstream payload is a 2-byte register, so no large buffers needed.

// ---------------------- TCA9546A mux ----------------------

#if FG_USE_MUX
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
#endif

/**
 * \brief Opens exactly one mux channel (0-3) and verifies it took.
 * \return SUCCESS, or ERROR_I2C_FAILED / ERROR_SANITY_CHECK_FAILED.
 */
static status_t fg_mux_select(uint8_t channel) {
    if (channel >= FG_NUM_PACKS) {
        return ERROR_BAD_TARGET;
    }

#if FG_USE_MUX
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
#endif
    return SUCCESS;
}

// Closes all mux channels so the downstream MAX addresses stop shadowing the shared bus.
static void fg_mux_disable(void) {
#if FG_USE_MUX
    if (fg_mux_raw_write(FG_MUX_CH_NONE) != 0) {
        warning("fuel_gauge: mux disable (close all channels) failed\n");
    }
#endif
}

// ---------------------- MAX17205 register access ----------------------

// Maps an ASF I2C return code (hal_i2c_m_sync.h) to a short description, so a failed
// transfer says *why* instead of collapsing to a generic error. The key distinction:
// NACK = nobody acknowledged the address (chip silent/absent), vs BUS = an electrical
// fault on the lines.
static const char *fg_i2c_err_str(int32_t rc) {
    switch (rc) {
        case I2C_OK: return "OK";
        case I2C_ACK: return "ACK";
        case I2C_NACK: return "NACK (no device ACKed this address)";
        case I2C_ERR_ARBLOST: return "ARBLOST (arbitration lost)";
        case I2C_ERR_BAD_ADDRESS: return "BAD_ADDRESS";
        case I2C_ERR_BUS: return "BUS ERROR (SDA/SCL electrical fault)";
        case I2C_ERR_BUSY: return "BUSY";
        case I2c_ERR_PACKAGE_COLLISION: return "PACKAGE_COLLISION";
        default: return "unknown";
    }
}

// Reads a 16-bit little-endian register from the currently-selected gauge.
static status_t fg_read_reg16(uint16_t reg, uint16_t *out) {
    uint8_t slave = (reg < 0x100) ? FG_MAX_ADDR_LOW : FG_MAX_ADDR_HIGH;
    uint8_t reg_byte = (uint8_t)(reg & 0xFF);
    uint8_t buf[2] = {0};

    i2c_m_sync_set_slaveaddr(&FG_I2C, slave, I2C_M_SEVEN);
    int32_t rc = i2c_m_sync_cmd_read(&FG_I2C, reg_byte, buf, 2);
    if (rc != 0) {
        warning("fuel_gauge: read reg 0x%03x (slave 0x%02x) failed rc=%ld %s\n", reg, slave, (long)rc,
                fg_i2c_err_str(rc));
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
 * \brief Brings up the ALRT input and probes the configured gauges.
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

    info("fuel_gauge: mode=%s packs=%u (SERCOM6 SDA=PD09 SCL=PD08)\n",
         FG_USE_MUX ? "TCA9546A mux" : "direct", FG_NUM_PACKS);
    status_t result = SUCCESS;
    for (uint8_t ch = 0; ch < FG_NUM_PACKS; ch++) {
        if (fg_mux_select(ch) != SUCCESS) {
            warning("fuel_gauge: gauge %u unreachable (channel select failed)\n", ch + 1);
            result = ERROR_I2C_FAILED;
            continue;
        }

        uint16_t devname = 0;
        if (fg_read_reg16(MAX17205_REG_DEVNAME, &devname) != SUCCESS) {
            result = ERROR_I2C_FAILED;
            continue;
        }
        info("fuel_gauge: gauge %u DevName=0x%04x (I2C register read OK)\n", ch + 1, devname);

        uint16_t status = 0, packcfg = 0, npackcfg = 0, designcap = 0, ndesigncap = 0;
        status_t rs = SUCCESS, rp = SUCCESS, rn = SUCCESS, rd = SUCCESS, rnd = SUCCESS;

        rs = fg_read_reg16(MAX17205_REG_STATUS, &status);
        rp = fg_read_reg16(MAX17205_REG_PACKCFG, &packcfg);
        rn = fg_read_reg16(MAX17205_REG_NPACKCFG, &npackcfg);
        rd = fg_read_reg16(MAX17205_REG_DESIGNCAP, &designcap);
        rnd = fg_read_reg16(MAX17205_REG_NDESIGNCAP, &ndesigncap);

        if (rs != SUCCESS || rp != SUCCESS || rn != SUCCESS || rd != SUCCESS || rnd != SUCCESS) {
            warning("fuel_gauge: gauge %u config read error(s)\n", ch + 1);
            result = ERROR_I2C_FAILED;
            continue;
        }

        // NCELLS occupies the low 4 bits of PackCfg/nPackCfg (Fig 37). Log raw + NCELLS.
        info("fuel_gauge: gauge %u: Status=0x%04x PackCfg=0x%04x (NCELLS=%u) "
             "nPackCfg=0x%04x (NCELLS=%u) DesignCap=0x%04x nDesignCap=0x%04x\n",
             ch + 1, status, packcfg, (unsigned)(packcfg & 0x000F), npackcfg,
             (unsigned)(npackcfg & 0x000F), designcap, ndesigncap);
    }

    fg_mux_disable();
    return result;
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

    memset(out, 0, sizeof(*out));
    ret_err_status(fg_mux_select(channel), "fuel_gauge: channel select failed in read_pack");

    uint16_t soc = 0, batt = 0, current = 0, temp = 0;
    ret_err_status(fg_read_reg16(MAX17205_REG_REPSOC, &soc), "fuel_gauge: RepSOC read failed");
    ret_err_status(fg_read_reg16(MAX17205_REG_BATT, &batt), "fuel_gauge: Batt read failed");
    ret_err_status(fg_read_reg16(MAX17205_REG_CURRENT, &current), "fuel_gauge: Current read failed");
    ret_err_status(fg_read_reg16(MAX17205_REG_TEMP, &temp), "fuel_gauge: Temp read failed");

    out->soc_pct = fg_conv_soc(soc);
    out->pack_voltage = fg_conv_batt(batt);
    out->current_a = fg_conv_current(current);
    out->temp_c = fg_conv_temp(temp);
    out->valid = true;
    return SUCCESS;
}

/**
 * \brief Reads the configured packs. Failed entries are zeroed and marked invalid.
 *        Closes the mux when done.
 */
status_t fg_read_all(fg_reading_t out[FG_NUM_PACKS]) {
    if (out == NULL) {
        return ERROR_BAD_TARGET;
    }

    status_t result = SUCCESS;
    for (uint8_t ch = 0; ch < FG_NUM_PACKS; ch++) {
        memset(&out[ch], 0, sizeof(out[ch]));
        if (fg_read_pack(ch, &out[ch]) != SUCCESS) {
            warning("fuel_gauge: read of pack %u failed\n", ch + 1);
            result = ERROR_I2C_FAILED;
        }
    }

    fg_mux_disable();
    return result;
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

// ---------------------- Bring-up diagnostic ----------------------

void fg_i2c_scan(void) {
    i2c_m_sync_enable(&FG_I2C);
    info("fuel_gauge: I2C scan (SERCOM6)...\n");

    uint8_t found = 0;
    // Avoid general-call/reserved addresses and unrelated devices on the shared bus.
    const uint8_t addresses[] = {FG_MAX_ADDR_LOW, FG_MAX_ADDR_HIGH,
#if FG_USE_MUX
                                 FG_MUX_ADDR,
#endif
    };
    for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
        uint8_t addr = addresses[i];
        // Presence probe via a WRITE: START, addr+W, one memory-address byte, STOP. The
        // MAX17205 read protocol requires a memory-address write before any read, so a bare
        // read is not a valid transaction for it and can false-negative; a write needs only
        // the address ACK and matches the datasheet write protocol. transfer() returns 0
        // iff the address was ACKed.
        uint8_t reg = 0x00;
        struct _i2c_m_msg msg = {.addr = addr, .flags = I2C_M_STOP, .len = 1, .buffer = &reg};
        if (i2c_m_sync_transfer(&FG_I2C, &msg) == 0) {
            info("fuel_gauge: ACK at 0x%02x\n", addr);
            found++;
        }
    }

    info("fuel_gauge: scan done (%u device(s) responded)\n", found);
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
