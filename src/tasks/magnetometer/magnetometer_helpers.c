/**
 * Code for the RM3100 Magnetometer Sensor
 *
 * Created: Dec 7, 2023 2:22 AM
 * Authors: Nathan Kim, Alexander Thaep, Siddharta Laloux, Tanish Makadia, Defne Doken, Aidan Wang
 */

#include "magnetometer_task.h"

extern magnetometer_task_memory_t magnetometer_mem;

// IO descriptor for the RM3100
#define I2C_SERCOM

struct io_descriptor *rm3100_io;
static rm3100_power_mode_t m_sensor_mode;
static uint16_t m_sample_rate;
static uint8_t m_cycle_count;
static int8_t m_samples[9];
static float m_xyz[3];
static float m_gain;

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

status_t mag_store(void) {
    // TODO: Once the DataStore is fully implemented, store the reading there.
}

rm3100_return_t mag_read(void) {

}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

status_t rm3100_read_reg(int32_t *p_bytes_read, uint8_t addr, uint8_t *val, uint16_t size);
status_t rm3100_byte_read_reg(uint8_t *p_read_buf, uint8_t addr);
status_t rm3100_write_reg(int32_t *p_bytes_written, uint8_t addr, uint8_t *data, uint16_t size);
status_t rm3100_gather_data(float *store_address);

void mag_change_cycle_count(uint16_t newCC);
rm3100_power_mode_t mag_set_power_mode(rm3100_power_mode_t mode);
uint16_t mag_set_sample_rate(uint16_t sample_rate);
rm3100_status_t mag_modify_interrupts(uint8_t cmm_value, uint8_t poll_value);

int init_rm3100(void);

rm3100_return_t mag_read_data();

// https://www.tri-m.com/products/pni/RM3100-User-Manual.pdf
// https://github.com/inventorandy/atmel-samd21/blob/master/07_I2CTSYS/07_I2CTSYS/ext_tsys01.h#L15
// https://os.mbed.com/users/ddelsuc/code/RM3100BB_Sample_Code/

// For future reference, PIN 51 -> SCL & PIN 52 -> SDA

int init_rm3100(void) {
    // Initialize I2C
    i2c_m_sync_set_baudrate(&I2C_0, 0, 115200);
    i2c_m_sync_get_io_descriptor(&I2C_0, &rm3100_io);
    i2c_m_sync_enable(&I2C_0);
    i2c_m_sync_set_slaveaddr(&I2C_0, RM3100_ADDRESS, I2C_M_SEVEN);

    uint8_t init_values[4] = {0, 0, 0, 0};
    uint8_t cycle_values[2] = {0, 0};

    init_values[0] = rm3100_byte_read_reg(RM3100_REVID_REG);
    init_values[1] = rm3100_byte_read_reg(RM3100_HSHAKE_REG);

    if (init_values[0] != RM3100_REVID_VALUE || init_values[1] != RM3100_HSHAKE_VALUE) {
        return SENSOR_I2C_ERROR;
    }

    if (rm3100_read_reg(RM3100_LROSCADJ_REG, &init_values[2], 2) < SENSOR_OK) {
        return SENSOR_ERROR_NONEXISTANT;
    }

    if ((init_values[2] != RM3100_LROSCADJ_VALUE) || (init_values[3] != RM3100_SLPOSCADJ_VALUE)) {
        return ERROR_UNEXPECTED_DEVICE;
    }

    mag_change_cycle_count(INITIAL_CC);
    rm3100_read_reg(RM3100_CCX1_REG, &cycle_values[0], 1);
    rm3100_read_reg(RM3100_CCX0_REG, &cycle_values[1], 1);

    m_cycle_count = cycle_values[0];
    m_cycle_count = (m_cycle_count << 8) | cycle_values[1];

    if (!SINGLE_MODE) {
        mag_set_power_mode(SENSOR_POWER_MODE_CONTINUOUS);
        mag_set_sample_rate(SAMPLE_RATE);
    } else {
        mag_set_power_mode(SENSOR_POWER_MODE_SINGLE);
    }

    return SENSOR_OK;
}

/**
 * \brief Read a single byte from the RM3100
 * 
 * \param p_read_buf Pointer to the buffer to store the read byte
 * \param addr Address of the register to read from
 * \return status_t SUCCESS if the read was successful, ERROR_READ_FAILED otherwise
 * 
 * \note This function is a wrapper around `rm3100_read_reg` that reads a single byte from the RM3100
 */
status_t rm3100_byte_read_reg(uint8_t *p_read_buf, uint8_t addr) {
    uint8_t read_buf[1] = {0};

    status_t status;
    int32_t bytes_read;
    if ((status = rm3100_read_reg(&bytes_read, addr, read_buf, 1)) != SUCCESS)
        return status;
    
    if (bytes_read != 1) {
        return ERROR_READ_FAILED;
    }

    *p_read_buf = read_buf[0];
    return SUCCESS;
}

/**
 * \brief Read a register from the RM3100
 * 
 * \param p_bytes_read Pointer to a uint32_t to store the number of bytes read
 * \param addr Address of the register to read from
 * \param read_buf Buffer to store the read data
 * \param size Number of bytes to read
 * \return status_t SUCCESS if the read was successful, or ERROR_READ_FAILED / ERROR_WRITE_FAILED otherwise
 */
status_t rm3100_read_reg(int32_t *p_bytes_read, uint8_t addr, uint8_t *read_buf, uint16_t size) {
    uint8_t write_buf[1] = {addr};
    int32_t rv;

    if ((rv = io_write(rm3100_io, write_buf, 1)) < 0) {
        warning("Error in RM3100 Write");
        return ERROR_WRITE_FAILED;
    }
    if ((rv = io_read(rm3100_io, read_buf, size)) < 0) {
        warning("Error in RM3100 Read");
        return ERROR_READ_FAILED;
    }
    
    *p_bytes_read = rv;
    return SUCCESS;
}

status_t rm3100_gather_data(float *store_address) {
    uint8_t data[] = {REQUEST};

    status_t status;
    int32_t bytes_written;
    if ((status = rm3100_write_reg(&bytes_written, RM3100_POLL_REG, data, 1)) != SUCCESS) 
        return status;

    if (bytes_written != 1) {
        return ERROR_WRITE_FAILED;
    }

    store_address[0] = m_xyz[0];
    store_address[1] = m_xyz[1];
    store_address[2] = m_xyz[2];

    return SUCCESS;
}

status_t rm3100_write_reg(int32_t *p_bytes_written, uint8_t addr, uint8_t *data, uint16_t size) {
    uint8_t write_buf[MAX_I2C_WRITE + 1];
    int32_t rv;

    write_buf[0] = addr;
    memcpy(&(write_buf[1]), data, size);
    if ((rv = io_write(rm3100_io, write_buf, size + 1)) < 0) {
        warning("Error in RM3100 Write");
        return ERROR_WRITE_FAILED;
    }

    *p_bytes_written = rv;
    return SUCCESS;
}

rm3100_return_t mag_read_data() {
    rm3100_return_t readings;

    // read out sensor data
    rm3100_read_reg(RM3100_QX2_REG, (uint8_t *)&m_samples, sizeof(m_samples) / sizeof(char));

    readings.x = ((int8_t)m_samples[0]) * 256 * 256;
    readings.x |= m_samples[1] * 256;
    readings.x |= m_samples[2];

    readings.y = ((int8_t)m_samples[3]) * 256 * 256;
    readings.y |= m_samples[4] * 256;
    readings.y |= m_samples[5];

    readings.z = ((int8_t)m_samples[6]) * 256 * 256;
    readings.z |= m_samples[7] * 256;
    readings.z |= m_samples[8];

    // stow sensor data
    m_xyz[0] = (float)readings.x / m_gain;
    m_xyz[1] = (float)readings.y / m_gain;
    m_xyz[2] = (float)readings.z / m_gain;

    return readings;
}

rm3100_status_t mag_modify_interrupts(uint8_t cmm_value, uint8_t poll_value) {
    uint8_t data[2] = {cmm_value, poll_value};

    rm3100_write_reg(RM3100_CMM_REG, &data[0], 1);
    rm3100_write_reg(RM3100_POLL_REG, &data[1], 1);
    return SENSOR_OK;
}

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

    rm3100_write_reg(RM3100_TMRC_REG, i2c_buffer, 1);

    if (m_sensor_mode == SENSOR_POWER_MODE_CONTINUOUS) {
        mag_modify_interrupts(RM3100_ENABLED, RM3100_DISABLED);
    }

    rm3100_read_reg(RM3100_TMRC_REG, i2c_buffer, 1);

    return i2c_buffer[0];
}

void mag_change_cycle_count(uint16_t newCC) {
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
    rm3100_write_reg(RM3100_CCX1_REG, settings, 6);
}

void exec_command_magnetometer(command_t *const p_cmd) {
    if (p_cmd->target != p_magnetometer_task) {
        fatal("magnetometer: command target is not watchdog! target: %d operation: %d\n", p_cmd->target->name, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        case OPERATION_READ:
            p_cmd -> result = magnetometer_read();
            break;
        default:
            fatal("magnetometer: Invalid operation! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
            break;
    }
    
}

QueueHandle_t init_magnetometer(void) {
    init_rm3100();

    // Initialize the magnetometer command queue
    QueueHandle_t magnetometer_command_queue_handle =
        xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, magnetometer_mem.magnetometer_command_queue_buffer,
                           &magnetometer_mem.magnetometer_task_queue);
    if (magnetometer_command_queue_handle == NULL) {
        fatal("Failed to create magnetometer queue!\n");
    }

    return magnetometer_command_queue_handle;
}
