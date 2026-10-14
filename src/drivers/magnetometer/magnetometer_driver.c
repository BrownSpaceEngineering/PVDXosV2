/**
 * magnetometer_driver.c
 *
 * Driver for the RM3100 Magnetometer Sensor from PNICorp — SPI variant.
 *
 * Created: Dec 7, 2023 2:22 AM
 * Authors: Nathan Kim, Alexander Thaep, Siddharta Laloux, Tanish Makadia, Defne Doken, Aidan Wang
 *
 * Shared SPI_MAGNETOMETER_GYRO bus (SERCOM4): SCK PA12, MISO PB14,
 * MOSI PA13, active-low MAGNETOMETER_CS PC19. Tie I2CEN low for SPI.
 * Main's ASF config supplies mode 0, 8-bit MSB-first transfers at 50 kHz.
 * The RM3100 supports modes 0/3 at <= 1 MHz. During the command byte it
 * returns STATUS; subsequent bytes contain auto-incremented register data.
 */

#include "magnetometer_driver.h"

#include "mutexes.h"

#define RM3100_SPI SPI_MAGNETOMETER_GYRO
#define RM3100_CS_PIN MAGNETOMETER_CS
#define RM3100_SPI_MAX 16
#define RM3100_SPI_READ 0x80
#define RM3100_STATUS_DRDY 0x80

static rm3100_power_mode_t m_sensor_mode;
static uint16_t m_sample_rate;
static uint16_t m_cycle_count;
static float m_gain;
static bool m_initialized;

static inline void rm3100_cs_select(void) {
    gpio_set_pin_level(RM3100_CS_PIN, false);
}

static inline void rm3100_cs_deselect(void) {
    gpio_set_pin_level(RM3100_CS_PIN, true);
}

status_t init_rm3100(void) {
    m_initialized = false;
    m_gain = 0.0f;
    m_cycle_count = 0;
    m_sample_rate = 0;

    // Register access initializes/enables the shared bus under its common lock.
    uint8_t revid = 0;
    ret_err_status(rm3100_read_reg(NULL, RM3100_REVID_REG, &revid, 1), "magnetometer: SPI read of REVID failed during initialization\n");
    if (revid != RM3100_REVID_VALUE) {
        warning("magnetometer: unexpected REVID 0x%02x (expected 0x%02x)\n", revid, RM3100_REVID_VALUE);
        return ERROR_SANITY_CHECK_FAILED;
    }

    if (mag_set_power_mode(SENSOR_POWER_MODE_INACTIVE) == SENSOR_POWER_MODE_ERROR) {
        return ERROR_WRITE_FAILED;
    }
    ret_err_status(mag_change_cycle_count(INITIAL_CC), "magnetometer: setting cycle count failed\n");

    uint8_t cycle_values[6];
    ret_err_status(rm3100_read_reg(NULL, RM3100_CCX1_REG, cycle_values, sizeof(cycle_values)),
                   "magnetometer: cycle count readback failed\n");
    for (size_t i = 0; i < 3; i++) {
        uint16_t cycle_count = ((uint16_t)cycle_values[2 * i] << 8) | cycle_values[2 * i + 1];
        if (cycle_count != m_cycle_count) {
            warning("magnetometer: axis %u cycle count %u != expected %u\n", (unsigned)i, cycle_count, m_cycle_count);
            return ERROR_SANITY_CHECK_FAILED;
        }
    }

    // Configure TMRC before starting CMM: writing TMRC stops continuous measurements.
    if (mag_set_sample_rate(SAMPLE_RATE) == 0) {
        return ERROR_WRITE_FAILED;
    }
    rm3100_power_mode_t mode = SINGLE_MODE ? SENSOR_POWER_MODE_SINGLE : SENSOR_POWER_MODE_CONTINUOUS;
    if (mag_set_power_mode(mode) == SENSOR_POWER_MODE_ERROR) {
        return ERROR_WRITE_FAILED;
    }

    m_initialized = true;
    return SUCCESS;
}

/** Read register data, discarding the STATUS byte received during the command. */
status_t rm3100_read_reg(int32_t *p_bytes_read, uint8_t addr, uint8_t *read_buf, uint16_t size) {
    if (p_bytes_read != NULL) {
        *p_bytes_read = 0;
    }
    if (read_buf == NULL || size == 0 || size > RM3100_SPI_MAX - 1) {
        return ERROR_BAD_ARGS;
    }

    uint8_t tx[RM3100_SPI_MAX] = {0};
    uint8_t rx[RM3100_SPI_MAX] = {0};
    tx[0] = RM3100_SPI_READ | addr;
    struct spi_xfer xfer = {.txbuf = tx, .rxbuf = rx, .size = (uint32_t)size + 1};

    lock_magnetometer_gyro_spi();
    rm3100_cs_select();
    int32_t rv = spi_m_sync_transfer(&RM3100_SPI, &xfer);
    rm3100_cs_deselect();
    unlock_magnetometer_gyro_spi();
    if (rv != (int32_t)xfer.size) {
        warning("magnetometer: SPI read failed (addr=0x%02x, rv=%ld)\n", addr, (long)rv);
        return ERROR_READ_FAILED;
    }

    memcpy(read_buf, &rx[1], size);
    if (p_bytes_read != NULL) {
        *p_bytes_read = size;
    }
    return SUCCESS;
}

status_t rm3100_write_reg(int32_t *p_bytes_written, uint8_t addr, uint8_t *data, uint16_t size) {
    if (p_bytes_written != NULL) {
        *p_bytes_written = 0;
    }
    if (data == NULL || size == 0 || size > RM3100_SPI_MAX - 1) {
        return ERROR_BAD_ARGS;
    }

    uint8_t tx[RM3100_SPI_MAX] = {0};
    uint8_t rx[RM3100_SPI_MAX] = {0};
    tx[0] = addr & 0x7F;
    memcpy(&tx[1], data, size);
    struct spi_xfer xfer = {.txbuf = tx, .rxbuf = rx, .size = (uint32_t)size + 1};

    lock_magnetometer_gyro_spi();
    rm3100_cs_select();
    int32_t rv = spi_m_sync_transfer(&RM3100_SPI, &xfer);
    rm3100_cs_deselect();
    unlock_magnetometer_gyro_spi();
    if (rv != (int32_t)xfer.size) {
        warning("magnetometer: SPI write failed (addr=0x%02x, rv=%ld)\n", addr, (long)rv);
        return ERROR_WRITE_FAILED;
    }

    if (p_bytes_written != NULL) {
        *p_bytes_written = size;
    }
    return SUCCESS;
}

static int32_t rm3100_decode_axis(const uint8_t *sample) {
    int32_t value = ((int32_t)sample[0] << 16) | ((int32_t)sample[1] << 8) | sample[2];
    // Only bit 23 is the sign bit; the lower two bytes must remain unsigned.
    return (value & 0x800000) ? value - 0x1000000 : value;
}

/** Read signed 24-bit counts and convert to microteslas (gain is counts/uT). */
status_t mag_read_data(mag_raw_reading_t *const raw_readings, mag_data_t *const gain_adj_readings) {
    if (!m_initialized) {
        return ERROR_NOT_READY;
    }
    uint8_t samples[9];
    ret_err_status(rm3100_read_reg(NULL, RM3100_QX2_REG, samples, sizeof(samples)), "magnetometer: read from QX2 register failed\n");

    mag_raw_reading_t readings = {
        .x = rm3100_decode_axis(&samples[0]),
        .y = rm3100_decode_axis(&samples[3]),
        .z = rm3100_decode_axis(&samples[6]),
    };
    if (raw_readings != NULL) {
        *raw_readings = readings;
    }
    if (gain_adj_readings != NULL) {
        gain_adj_readings->x = (float)readings.x / m_gain;
        gain_adj_readings->y = (float)readings.y / m_gain;
        gain_adj_readings->z = (float)readings.z / m_gain;
    }
    return SUCCESS;
}

status_t mag_modify_interrupts(uint8_t cmm_value, uint8_t poll_value) {
    ret_err_status(rm3100_write_reg(NULL, RM3100_CMM_REG, &cmm_value, 1), "magnetometer: write to CMM register failed\n");
    ret_err_status(rm3100_write_reg(NULL, RM3100_POLL_REG, &poll_value, 1), "magnetometer: write to POLL register failed\n");
    return SUCCESS;
}

rm3100_power_mode_t mag_set_power_mode(rm3100_power_mode_t mode) {
    uint8_t cmm_value = RM3100_DISABLED;
    uint8_t poll_value = RM3100_DISABLED;
    switch (mode) {
        case SENSOR_POWER_MODE_INACTIVE:
            break;
        case SENSOR_POWER_MODE_CONTINUOUS:
            cmm_value = RM3100_ENABLED;
            break;
        case SENSOR_POWER_MODE_SINGLE:
            poll_value = RM3100_SINGLE;
            break;
        default:
            return SENSOR_POWER_MODE_ERROR;
    }

    if (mag_modify_interrupts(cmm_value, poll_value) != SUCCESS) {
        m_sensor_mode = SENSOR_POWER_MODE_ERROR;
        m_initialized = false;
        return SENSOR_POWER_MODE_ERROR;
    }
    m_sensor_mode = mode;
    return m_sensor_mode;
}

uint16_t mag_set_sample_rate(uint16_t sample_rate) {
    const uint16_t supported_rates[][2] = {
        // Approximate Hz (rounded down), TMRC register value. Upper nibble must be 0x9.
        {2, 0x9A}, {4, 0x99}, {9, 0x98}, {18, 0x97}, {37, 0x96}, {75, 0x95}, {150, 0x94}, {300, 0x93},
    };
    size_t i;
    for (i = 0; i < sizeof(supported_rates) / sizeof(supported_rates[0]) - 1; i++) {
        if (sample_rate <= supported_rates[i][0]) {
            break;
        }
    }

    bool continuous = m_sensor_mode == SENSOR_POWER_MODE_CONTINUOUS;
    if (continuous && mag_set_power_mode(SENSOR_POWER_MODE_INACTIVE) == SENSOR_POWER_MODE_ERROR) {
        return 0;
    }

    uint8_t tmrc = (uint8_t)supported_rates[i][1];
    uint8_t readback = 0;
    if (rm3100_write_reg(NULL, RM3100_TMRC_REG, &tmrc, 1) != SUCCESS || rm3100_read_reg(NULL, RM3100_TMRC_REG, &readback, 1) != SUCCESS ||
        readback != tmrc) {
        warning("magnetometer: TMRC configuration/readback failed\n");
        m_sample_rate = 0;
        m_initialized = false;
        return 0;
    }
    if (continuous && mag_set_power_mode(SENSOR_POWER_MODE_CONTINUOUS) == SENSOR_POWER_MODE_ERROR) {
        return 0;
    }

    m_sample_rate = supported_rates[i][0];
    return m_sample_rate;
}

status_t mag_change_cycle_count(uint16_t newCC) {
    if (newCC == 0) {
        return ERROR_BAD_ARGS;
    }
    uint8_t msb = (uint8_t)(newCC >> 8);
    uint8_t lsb = (uint8_t)newCC;
    uint8_t settings[6] = {msb, lsb, msb, lsb, msb, lsb};
    status_t status = rm3100_write_reg(NULL, RM3100_CCX1_REG, settings, sizeof(settings));
    if (status != SUCCESS) {
        m_initialized = false;
        return status;
    }
    m_cycle_count = newCC;
    m_gain = 0.3671f * m_cycle_count + 1.5f;
    return SUCCESS;
}

/** Poll STATUS rather than requiring a separate DRDY wire. */
status_t magnetometer_read(mag_raw_reading_t *const raw_readings, mag_data_t *const gain_adj_readings) {
    if (!m_initialized || m_sensor_mode == SENSOR_POWER_MODE_INACTIVE) {
        return ERROR_NOT_READY;
    }
    uint8_t status = 0;
    ret_err_status(rm3100_read_reg(NULL, RM3100_STATUS_REG, &status, 1), "magnetometer: read from STATUS register failed\n");
    if (!(status & RM3100_STATUS_DRDY)) {
        return ERROR_NOT_READY;
    }
    return mag_read_data(raw_readings, gain_adj_readings);
}
