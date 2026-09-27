/**
 * magnetometer_driver.c
 *
 * Driver for the RM3100 Magnetometer Sensor from PNICorp — SPI variant.
 *
 * Created: Dec 7, 2023 2:22 AM
 * Authors: Nathan Kim, Alexander Thaep, Siddharta Laloux, Tanish Makadia, Defne Doken, Aidan Wang
 *
 * Bare-metal debug build: the RM3100 is driven over SPI on the pre-configured
 * SPI_DISPLAY bus (SERCOM1). Wiring is given by SAMD51 port pin, not board label:
 *
 *     RM3100 pin        signal   SAMD51 pin (SERCOM1)
 *     ------------------------------------------------
 *     1  SCK            SCLK     PC23   (SERCOM1 PAD1)
 *     2  SO  (MISO)     MISO     PA18   (SERCOM1 PAD2)
 *     3  SI  (MOSI)     MOSI     PC22   (SERCOM1 PAD0)
 *     4  SSN (CS)       CS       PB13   (GPIO, driven manually, active LOW)
 *     10 I2CEN                   -> tie LOW (selects SPI mode)
 *     12 DVDD / 13 AVDD         -> 3.3V
 *     7  AVSS / 14 DVSS         -> GND
 *     5  DRDY                    -> not needed (we poll the STATUS register)
 *
 * The RM3100 is SPI mode 0 (CPOL=CPHA=0), clock <= 1 MHz. SERCOM1 is configured
 * for ~50 kHz in ASF, well within spec. A read sends (0x80 | reg); a write sends
 * (reg & 0x7F) followed by data. The chip auto-increments the register pointer,
 * and the first returned byte on any transfer is the STATUS/DRDY byte.
 */

#include "globals.h"

#include "magnetometer_driver.h"

// The RM3100 lives on the SPI_DISPLAY bus (SERCOM1); CS is DISPLAY_CS (PB13).
#define RM3100_SPI    SPI_DISPLAY
#define RM3100_CS_PIN DISPLAY_CS

// Largest single SPI payload we issue (9-byte measurement read) plus the
// leading command byte.
#define RM3100_SPI_MAX 16

// SPI read/write direction bit applied to the register address byte.
#define RM3100_SPI_READ 0x80

// STATUS register (0x34) bit 7 == DRDY (data ready).
#define RM3100_STATUS_DRDY 0x80

static rm3100_power_mode_t m_sensor_mode;
static uint16_t m_sample_rate;
static uint16_t m_cycle_count;
static float m_gain;

static inline void rm3100_cs_select(void) {
    gpio_set_pin_level(RM3100_CS_PIN, false); // active low
}

static inline void rm3100_cs_deselect(void) {
    gpio_set_pin_level(RM3100_CS_PIN, true);
}

/**
 * \fn init_rm3100
 *
 * \brief Brings up the SPI bus and the RM3100: verifies the REVID, programs the
 *        cycle count, and starts continuous measurement mode.
 *
 * \return `status_t` SUCCESS on success, otherwise an error status.
 */
status_t init_rm3100(void) {
    // Configure the SPI peripheral. set_mode()/set_baudrate() must run while the
    // peripheral is disabled; spi_m_sync_init() (called from atmel_start_init)
    // leaves it disabled, so set the mode first and then enable.
    spi_m_sync_set_mode(&RM3100_SPI, SPI_MODE_0);
    spi_m_sync_enable(&RM3100_SPI);

    // Drive CS as a GPIO output, idle high (deselected).
    gpio_set_pin_function(RM3100_CS_PIN, GPIO_PIN_FUNCTION_OFF);
    gpio_set_pin_direction(RM3100_CS_PIN, GPIO_DIRECTION_OUT);
    rm3100_cs_deselect();

    // Sanity check: read the revision ID. This is the definitive "is the sensor
    // wired up and talking" test.
    uint8_t revid = 0;
    if (rm3100_read_reg(NULL, RM3100_REVID_REG, &revid, 1) != SUCCESS) {
        warning("magnetometer: SPI read of REVID failed during initialization\n");
        return ERROR_SPI_TRANSFER_FAILED;
    }
    if (revid != RM3100_REVID_VALUE) {
        warning("magnetometer: unexpected REVID 0x%02x (expected 0x%02x)\n", revid, RM3100_REVID_VALUE);
        return ERROR_SANITY_CHECK_FAILED;
    }
    info("magnetometer: REVID = 0x%02x OK\n", revid);

    // Program the cycle count on all three axes.
    mag_change_cycle_count(INITIAL_CC);

    // Read it back as a sanity check.
    uint8_t cycle_values[2] = {0, 0};
    if (rm3100_read_reg(NULL, RM3100_CCX1_REG, cycle_values, 2) != SUCCESS) {
        warning("magnetometer: SPI read of CCX1 failed during initialization\n");
        return ERROR_SPI_TRANSFER_FAILED;
    }
    m_cycle_count = ((uint16_t)cycle_values[0] << 8) | cycle_values[1];
    if (m_cycle_count != INITIAL_CC) {
        warning("magnetometer: cycle count readback %u != expected %u\n", m_cycle_count, INITIAL_CC);
        return ERROR_SANITY_CHECK_FAILED;
    }

    // Gain (LSB/uT) as a function of cycle count.
    m_gain = 0.3671 * m_cycle_count + 1.5;

    // Start measuring.
    if (!SINGLE_MODE) {
        mag_set_power_mode(SENSOR_POWER_MODE_CONTINUOUS);
        mag_set_sample_rate(SAMPLE_RATE);
    } else {
        mag_set_power_mode(SENSOR_POWER_MODE_SINGLE);
    }

    return SUCCESS;
}

/**
 * \fn rm3100_read_reg
 *
 * \brief Reads `size` bytes starting at register `addr` over SPI.
 *
 * The transaction is: CS low, send (0x80 | addr), clock out `size` more bytes,
 * CS high. The first byte the RM3100 returns is the STATUS byte, so the register
 * data starts at rx[1].
 *
 * \param p_bytes_read If not NULL, receives the number of data bytes read.
 * \param addr Register address to read from.
 * \param read_buf Buffer to store the read data (`size` bytes).
 * \param size Number of register bytes to read.
 *
 * \return `status_t` SUCCESS on success, ERROR_READ_FAILED otherwise.
 */
status_t rm3100_read_reg(int32_t *p_bytes_read, uint8_t addr, uint8_t *read_buf, uint16_t size) {
    if (size == 0 || size > RM3100_SPI_MAX - 1) {
        return ERROR_READ_FAILED;
    }

    uint8_t tx[RM3100_SPI_MAX] = {0};
    uint8_t rx[RM3100_SPI_MAX] = {0};
    tx[0] = RM3100_SPI_READ | addr;

    struct spi_xfer xfer = {.txbuf = tx, .rxbuf = rx, .size = (uint32_t)size + 1};

    rm3100_cs_select();
    int32_t rv = spi_m_sync_transfer(&RM3100_SPI, &xfer);
    rm3100_cs_deselect();

    if (rv < 0) {
        warning("magnetometer: SPI read failed (addr=0x%02x, rv=%ld)\n", addr, (long)rv);
        return ERROR_READ_FAILED;
    }

    memcpy(read_buf, &rx[1], size); // drop the leading STATUS byte

    if (p_bytes_read != NULL) {
        *p_bytes_read = size;
    }
    return SUCCESS;
}

/**
 * \fn rm3100_write_reg
 *
 * \brief Writes `size` bytes to register `addr` over SPI.
 *
 * The transaction is: CS low, send (addr & 0x7F), send `size` data bytes, CS high.
 *
 * \param p_bytes_written If not NULL, receives the number of data bytes written.
 * \param addr Register address to write to.
 * \param data Data to write.
 * \param size Number of bytes to write.
 *
 * \return `status_t` SUCCESS on success, ERROR_WRITE_FAILED otherwise.
 */
status_t rm3100_write_reg(int32_t *p_bytes_written, uint8_t addr, uint8_t *data, uint16_t size) {
    if (size > RM3100_SPI_MAX - 1) {
        return ERROR_WRITE_FAILED;
    }

    uint8_t tx[RM3100_SPI_MAX] = {0};
    uint8_t rx[RM3100_SPI_MAX] = {0};
    tx[0] = addr & 0x7F; // write: MSB clear
    memcpy(&tx[1], data, size);

    struct spi_xfer xfer = {.txbuf = tx, .rxbuf = rx, .size = (uint32_t)size + 1};

    rm3100_cs_select();
    int32_t rv = spi_m_sync_transfer(&RM3100_SPI, &xfer);
    rm3100_cs_deselect();

    if (rv < 0) {
        warning("magnetometer: SPI write failed (addr=0x%02x, rv=%ld)\n", addr, (long)rv);
        return ERROR_WRITE_FAILED;
    }

    if (p_bytes_written != NULL) {
        *p_bytes_written = size;
    }
    return SUCCESS;
}

/**
 * \fn mag_read_data
 *
 * \brief Reads x,y,z magnetic field data (9 bytes, 24-bit signed per axis).
 *
 * \param raw_readings If not NULL, receives the raw counts.
 * \param gain_adj_readings If not NULL, receives the gain-adjusted values.
 *
 * \return `status_t` SUCCESS on success, otherwise an error status.
 */
status_t mag_read_data(mag_raw_reading_t *const raw_readings, mag_data_t *const gain_adj_readings) {
    int32_t readings[3];
    int8_t m_samples[9];

    ret_err_status(rm3100_read_reg(NULL, RM3100_QX2_REG, (uint8_t *)&m_samples, sizeof(m_samples)),
                   "magnetometer: Read from QX2 Register failed");

    readings[0] = ((int8_t)m_samples[0]) * 256 * 256;
    readings[0] |= m_samples[1] * 256;
    readings[0] |= m_samples[2];

    readings[1] = ((int8_t)m_samples[3]) * 256 * 256;
    readings[1] |= m_samples[4] * 256;
    readings[1] |= m_samples[5];

    readings[2] = ((int8_t)m_samples[6]) * 256 * 256;
    readings[2] |= m_samples[7] * 256;
    readings[2] |= m_samples[8];

    if (raw_readings != NULL) {
        raw_readings->x = readings[0];
        raw_readings->y = readings[1];
        raw_readings->z = readings[2];
    }

    if (gain_adj_readings != NULL) {
        gain_adj_readings->x = (float)readings[0] / m_gain;
        gain_adj_readings->y = (float)readings[1] / m_gain;
        gain_adj_readings->z = (float)readings[2] / m_gain;
    }

    return SUCCESS;
}

/**
 * \fn mag_modify_interrupts
 *
 * \brief Writes the CMM and POLL registers.
 *
 * \return `status_t` SUCCESS on success, otherwise an error status.
 */
status_t mag_modify_interrupts(uint8_t cmm_value, uint8_t poll_value) {
    uint8_t data[2] = {cmm_value, poll_value};

    ret_err_status(rm3100_write_reg(NULL, RM3100_CMM_REG, &data[0], 1), "magnetometer: Write to CMM Register failed");
    ret_err_status(rm3100_write_reg(NULL, RM3100_POLL_REG, &data[1], 1), "magnetometer: Write to Poll Register failed");

    return SUCCESS;
}

/**
 * \fn mag_set_power_mode
 *
 * \brief Sets the power/measurement mode of the RM3100.
 *
 * \return the power mode that was set.
 */
rm3100_power_mode_t mag_set_power_mode(rm3100_power_mode_t mode) {
    switch (mode) {
        case SENSOR_POWER_MODE_INACTIVE:
            mag_modify_interrupts(RM3100_DISABLED, RM3100_DISABLED);
            break;
        case SENSOR_POWER_MODE_CONTINUOUS:
            mag_modify_interrupts(RM3100_ENABLED, RM3100_DISABLED);
            break;
        case SENSOR_POWER_MODE_SINGLE:
            mag_modify_interrupts(RM3100_DISABLED, RM3100_SINGLE);
            break;
    }

    m_sensor_mode = mode;
    return m_sensor_mode;
}

/**
 * \fn mag_set_sample_rate
 *
 * \brief Sets the continuous-measurement-mode update rate (TMRC register).
 *
 * \return the sample rate that was set.
 */
uint16_t mag_set_sample_rate(uint16_t sample_rate) {
    uint64_t i;
    uint8_t i2c_buffer[1];
    const uint16_t supported_rates[][2] = {
        /* [Hz], register value */
        {2, 0x0A},   // up to 2Hz
        {4, 0x09},   // up to 4Hz
        {8, 0x08},   // up to 8Hz
        {16, 0x07},  // up to 16Hz
        {31, 0x06},  // up to 31Hz
        {62, 0x05},  // up to 62Hz
        {125, 0x04}, // up to 125Hz
        {220, 0x03}  // up to 250Hz
    };

    for (i = 0; i < sizeof(supported_rates) / (sizeof(uint16_t) * 2) - 1; i++) {
        if (sample_rate <= supported_rates[i][0])
            break;
    }

    if (m_sensor_mode == SENSOR_POWER_MODE_CONTINUOUS) {
        mag_modify_interrupts(RM3100_DISABLED, RM3100_DISABLED);
    }

    m_sample_rate = supported_rates[i][0];
    i2c_buffer[0] = (uint8_t)supported_rates[i][1];

    ret_err_status(rm3100_write_reg(NULL, RM3100_TMRC_REG, i2c_buffer, 1), "magnetometer: Write to TMRC Register failed");

    if (m_sensor_mode == SENSOR_POWER_MODE_CONTINUOUS) {
        mag_modify_interrupts(RM3100_ENABLED, RM3100_DISABLED);
    }

    if (rm3100_read_reg(NULL, RM3100_TMRC_REG, i2c_buffer, 1) != SUCCESS) {
        warning("magnetometer: Read from TMRC Register failed\n");
    }

    return i2c_buffer[0];
}

/**
 * \fn mag_change_cycle_count
 *
 * \brief Sets the cycle count on all three axes.
 *
 * \return `status_t` SUCCESS on success, otherwise an error status.
 */
status_t mag_change_cycle_count(uint16_t newCC) {
    uint8_t settings[6];

    uint8_t CCMSB = (newCC & 0xFF00) >> 8; // most significant byte
    uint8_t CCLSB = newCC & 0xFF;          // least significant byte

    settings[0] = CCMSB; /* CCPX1 */
    settings[1] = CCLSB; /* CCPX0 */
    settings[2] = CCMSB; /* CCPY1 */
    settings[3] = CCLSB; /* CCPY0 */
    settings[4] = CCMSB; /* CCPZ1 */
    settings[5] = CCLSB; /* CCPZ0 */

    ret_err_status(rm3100_write_reg(NULL, RM3100_CCX1_REG, settings, 6), "magnetometer: Write to CCX1 Register failed");

    return SUCCESS;
}

/**
 * \fn magnetometer_read
 *
 * \brief Reads X,Y,Z if the sensor reports data ready.
 *
 * Data-ready is checked by polling the STATUS register (bit 7) over SPI rather
 * than a dedicated DRDY pin, so no DRDY wire is required.
 *
 * \return `status_t` SUCCESS if a reading was taken, ERROR_NOT_READY if no new
 *         data is available yet, or an error status on a bus failure.
 */
status_t magnetometer_read(mag_raw_reading_t *const raw_readings, mag_data_t *const gain_adj_readings) {
    uint8_t status = 0;
    ret_err_status(rm3100_read_reg(NULL, RM3100_STATUS_REG, &status, 1), "magnetometer: Read from STATUS Register failed");

    if (!(status & RM3100_STATUS_DRDY)) {
        debug("magnetometer: data not ready yet (STATUS=0x%02x)\n", status);
        return ERROR_NOT_READY;
    }

    return mag_read_data(raw_readings, gain_adj_readings);
}
