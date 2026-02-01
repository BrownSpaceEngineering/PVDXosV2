/**
 * magnetometer_driver.c
 * 
 * Driver for the RM3100 Magnetometer Sensor from PNICorp
 *
 * Created: Dec 7, 2023 2:22 AM
 * Authors: Nathan Kim, Alexander Thaep, Siddharta Laloux, Tanish Makadia, Defne Doken, Aidan Wang
 */

#include "magnetometer_driver.h"

// https://www.tri-m.com/products/pni/RM3100-User-Manual.pdf
// https://github.com/inventorandy/atmel-samd21/blob/master/07_I2CTSYS/07_I2CTSYS/ext_tsys01.h#L15
// https://os.mbed.com/users/ddelsuc/code/RM3100BB_Sample_Code/

// For future reference, PIN 51 -> SCL & PIN 52 -> SDA

struct io_descriptor *rm3100_io;
static rm3100_power_mode_t m_sensor_mode;
static uint16_t m_sample_rate;
static uint16_t m_cycle_count;
static float m_gain;

/**
 * \fn init_rm3100
 * 
 * \brief Initializes the RM3100 magnetometer sensor by setting up the I2C interface, reading 
 *        the handshake and revision ID registers, and setting the cycle count and sample rate.
 * 
 * \return `status_t` SUCCESS if the initialization was successful, calls fatal() otherwise
 */
status_t init_rm3100(void) {
    // Initialize I2C
    i2c_m_sync_set_baudrate(&I2C_MAGNETOMETER_GYRO, 0, 115200);
    i2c_m_sync_get_io_descriptor(&I2C_MAGNETOMETER_GYRO, &rm3100_io);
    i2c_m_sync_enable(&I2C_MAGNETOMETER_GYRO);
    i2c_m_sync_set_slaveaddr(&I2C_MAGNETOMETER_GYRO, RM3100_ADDRESS, I2C_M_SEVEN);

    uint8_t init_values[4] = {0, 0, 0, 0};
    uint8_t cycle_values[2] = {0, 0};
    
    // Read the revision ID and handshake registers
    fatal_on_error(rm3100_read_reg(NULL, RM3100_REVID_REG, &init_values[0], 1), 
        "magnetometer: Error reading RM3100 RevID register during initialization");
    fatal_on_error(rm3100_read_reg(NULL, RM3100_HSHAKE_REG, &init_values[1], 1),
        "magnetometer: Error reading RM3100 handshake register during initialization");
    if (init_values[0] != RM3100_REVID_VALUE) {
        fatal("magnetometer: Unexpected RM3100 RevID value during initialization");
    } 
    if (init_values[1] != RM3100_HSHAKE_VALUE) {
        fatal("magnetometer: Unexpected RM3100 handshake value during initialization");
    }
    
    // Read the LROSCADJ and SLPOSCADJ registers
    fatal_on_error(rm3100_read_reg(NULL, RM3100_LROSCADJ_REG, &init_values[2], 2),
        "magnetometer: Error reading RM3100 LROSCADJ register during initialization");
    if (init_values[2] != RM3100_LROSCADJ_VALUE) {
        fatal("magnetometer: Unexpected RM3100 LROSCADJ register value during initialization");
    }
    if (init_values[3] != RM3100_SLPOSCADJ_VALUE) {
        fatal("magnetometer: Unexpected RM3100 SLPOSCADJ register value during initialization");
    }

    // Set the cycle count for all three magnetometer axes
    mag_change_cycle_count(INITIAL_CC);

    // Attempt to read back the cycle count we just set from one axis as a sanity check
    fatal_on_error(rm3100_read_reg(NULL, RM3100_CCX1_REG, &cycle_values[0], 2),
        "magnetometer: Error reading first part of RM3100 CCX1 cycle-count register during initialization");

    m_cycle_count = cycle_values[0];
    m_cycle_count = (m_cycle_count << 8) | cycle_values[1];
        
    if (m_cycle_count != INITIAL_CC) {
        fatal("magnetometer: Cycle count value read from RM3100 X-axis does not match expected value");
    }

    // Calculate the gain using the cycle count
    m_gain = 0.3671 * m_cycle_count + 1.5;

    // Set the power mode and sample rate
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
 * \brief Reads a register from the RM3100
 *
 * \param p_bytes_read Pointer to a uint32_t to store the number of bytes read.
 *      If NULL, this function returns ERROR_READ_FAILED if the number of bytes
 *      written is not equal to `size`.
 * \param addr Address of the register to read from
 * \param read_buf Buffer to store the read data
 * \param size Number of bytes to read
 * 
 * \return `status_t` SUCCESS if the read was successful, or ERROR_READ_FAILED / ERROR_WRITE_FAILED otherwise
 */
status_t rm3100_read_reg(int32_t *p_bytes_read, uint8_t addr, uint8_t *read_buf, uint16_t size) {
    uint8_t write_buf[1] = {addr};
    int32_t rv;

    if ((rv = io_write(rm3100_io, write_buf, 1)) < 0) {
        warning("magnetometer: Error in RM3100 Write");
        return ERROR_WRITE_FAILED;
    }
    if ((rv = io_read(rm3100_io, read_buf, size)) < 0) {
        warning("magnetometer: Error in RM3100 Read");
        return ERROR_READ_FAILED;
    }
    
    if (p_bytes_read != NULL) {
        *p_bytes_read = rv;
    } else {
        if (rv != size) return ERROR_READ_FAILED;
    }

    return SUCCESS;
}

/**
 * \fn rm3100_write_reg
 * 
 * \brief Writes to a register on the RM3100
 * 
 * \param p_bytes_written Pointer to a uint32_t to store the number of bytes written.
 *      If NULL, this function returns ERROR_WRITE_FAILED if the number of bytes
 *      written is not equal to `size`.
 * \param addr Address of the register to write to
 * \param data Data to write to the register
 * \param size Number of bytes to write
 * 
 * \return `status_t` SUCCESS if the write was successful, or ERROR_WRITE_FAILED otherwise
 */
status_t rm3100_write_reg(int32_t *p_bytes_written, uint8_t addr, uint8_t *data, uint16_t size) {
    uint8_t write_buf[MAX_I2C_WRITE + 1];
    int32_t rv;

    write_buf[0] = addr;
    memcpy(&(write_buf[1]), data, size);
    if ((rv = io_write(rm3100_io, write_buf, size + 1)) < 0) {
        warning("magnetometer: Error in RM3100 Write");
        return ERROR_WRITE_FAILED;
    }

    if (p_bytes_written != NULL) {
        *p_bytes_written = rv;
    } else {
        if (rv != size) return ERROR_WRITE_FAILED;
    }

    return SUCCESS;
}

/**
 * \fn mag_read_data
 * 
 * \brief Reads x,y,z magnetic field data from the RM3100
 *
 * \param raw_readings If not NULL, pointer to a buffer (int32_t array of size
 *      3) to store the raw readings from the magnetometer.
 * \param gain_adj_readings If not NULL, pointer to a buffer (float array of
 *      size 3) to store the gain-adjusted readings from the magnetometer.
 * 
 * \return `status_t` SUCCESS if the read was successful, or ERROR_READ_FAILED/ERROR_WRITE_FAILED otherwise
 */
status_t mag_read_data(int32_t *const raw_readings, float *const gain_adj_readings) {
    int32_t readings[3];
    int8_t m_samples[9];
    
    // read out sensor data
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
        raw_readings[0] = readings[0];
        raw_readings[1] = readings[1];
        raw_readings[2] = readings[2];
    }

    // adjust the readings based on the gain
    if (gain_adj_readings != NULL) {
        gain_adj_readings[0] = (float)readings[0] / m_gain;
        gain_adj_readings[1] = (float)readings[1] / m_gain;
        gain_adj_readings[2] = (float)readings[2] / m_gain;
    }

    return SUCCESS;
}

/**
 * \fn mag_modify_interrupts

 * \brief Modifies the RM3100's interrupt settings
 * 
 * \param cmm_value Value to write to the CMM (Continuous Measurement Mode) register
 * \param poll_value Value to write to the POLL register
 * 
 * \return `status_t` SUCCESS if the write was successful, or ERROR_WRITE_FAILED otherwise
 */
status_t mag_modify_interrupts(uint8_t cmm_value, uint8_t poll_value) {
    uint8_t data[2] = {cmm_value, poll_value};

    ret_err_status(rm3100_write_reg(NULL, RM3100_CMM_REG, &data[0], 1),
        "magnetometer: Write to CMM Register failed");
    ret_err_status(rm3100_write_reg(NULL, RM3100_POLL_REG, &data[1], 1),
        "magnetometer: Read from Poll Register failed");
    
    return SUCCESS;
}

/**
 * \fn mag_set_power_mode
 * 
 * \brief Sets the power mode of the RM3100 magnetometer
 * 
 * \param mode The power mode to set the RM3100 to
 * 
 * \return rm3100_power_mode_t The power mode the RM3100 was set to
 */
rm3100_power_mode_t mag_set_power_mode(rm3100_power_mode_t mode) {
    switch (mode) {
        case SENSOR_POWER_MODE_INACTIVE:
            m_sensor_mode = mode;
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
 * \brief Sets the sample rate of the RM3100 magnetometer
 * 
 * \param sample_rate The sample rate to set the RM3100 to
 * 
 * \return uint16_t The sample rate the RM3100 was set to
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

    ret_err_status(rm3100_write_reg(NULL, RM3100_TMRC_REG, i2c_buffer, 1),
        "magnetometer: Write to TMRC Register failed");

    if (m_sensor_mode == SENSOR_POWER_MODE_CONTINUOUS) {
        mag_modify_interrupts(RM3100_ENABLED, RM3100_DISABLED);
    }

    fatal_on_error(rm3100_read_reg(NULL, RM3100_TMRC_REG, i2c_buffer, 1),
        "magnetometer: Read from TMRC Register failed");

    return i2c_buffer[0];
}

/**
 * \fn mag_change_cycle_count
 * 
 * \brief Changes the cycle count of the RM3100 magnetometer
 * 
 * \param newCC The new cycle count to set the RM3100 to
 * 
 * \return `status_t` SUCCESS if the write was successful, or ERROR_WRITE_FAILED otherwise
 */
status_t mag_change_cycle_count(uint16_t newCC) {
    uint8_t settings[6];

    uint8_t CCMSB = (newCC & 0xFF00) >> 8; // get the most significant byte
    uint8_t CCLSB = newCC & 0xFF;          // get the least significant byte

    /* Initialize settings */
    settings[0] = CCMSB; /* CCPX1 */
    settings[1] = CCLSB; /* CCPX0 */
    settings[2] = CCMSB; /* CCPY1 */
    settings[3] = CCLSB; /* CCPY0 */
    settings[4] = CCMSB; /* CCPZ1 */
    settings[5] = CCLSB; /* CCPZ0 */

    /*  Write register settings */
    ret_err_status(rm3100_write_reg(NULL, RM3100_CCX1_REG, settings, 6),
        "magnetometer: Write to CCX1 Register failed");
    
    return SUCCESS;
}