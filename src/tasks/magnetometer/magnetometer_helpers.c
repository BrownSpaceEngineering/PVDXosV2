/**
 * Code for the RM3100 Magnetometer Sensor
 *
 * Created: Dec 7, 2023 2:22 AM
 * Authors: Nathan Kim, Alexander Thaep, Siddharta Laloux
 **/

#include "magnetometer_task.h"

extern magnetometer_task_memory_t magnetometer_mem;

// IO descriptor for the RM3100
#define I2C_SERCOM

struct io_descriptor *rm3100_io;
static SensorPowerMode mSensorMode;
static uint16_t mSampleRate;
static uint8_t mCycleCount;
static int8_t mSamples[9];
static float mXYZ[3];
static float mGain;

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

// TODO: Implement a read function that reads the data from the RM3100

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

int32_t RM3100ReadReg(uint8_t addr, uint8_t *val, uint16_t size);
uint8_t RM3100ByteReadReg(uint8_t addr);
int32_t RM3100WriteReg(uint8_t addr, uint8_t *data, uint16_t size);
void RM3100GatherData(float *storeAddress);

void mag_change_cycle_count(uint16_t newCC);
SensorPowerMode mag_set_power_mode(SensorPowerMode mode);
uint16_t mag_set_sample_rate(uint16_t sample_rate);
SensorStatus mag_modify_interrupts(uint8_t cmm_value, uint8_t poll_value);

int init_rm3100(void);

RM3100_return_t mag_read_data();

// https://www.tri-m.com/products/pni/RM3100-User-Manual.pdf
// https://github.com/inventorandy/atmel-samd21/blob/master/07_I2CTSYS/07_I2CTSYS/ext_tsys01.h#L15
// https://os.mbed.com/users/ddelsuc/code/RM3100BB_Sample_Code/

// For future reference, PIN 51 -> SCL & PIN 52 -> SDA

int init_rm3100(void) {
    // Initialize I2C
    i2c_m_sync_set_baudrate(&I2C_0, 0, 115200);
    i2c_m_sync_get_io_descriptor(&I2C_0, &rm3100_io);
    i2c_m_sync_enable(&I2C_0);
    i2c_m_sync_set_slaveaddr(&I2C_0, RM3100Address, I2C_M_SEVEN);

    uint8_t init_values[4] = {0, 0, 0, 0};
    uint8_t cycle_values[2] = {0, 0};

    init_values[0] = RM3100ByteReadReg(RM3100_REVID_REG);
    init_values[1] = RM3100ByteReadReg(RM3100_HSHAKE_REG);

    if (init_values[0] != RM3100_REVID_VALUE || init_values[1] != RM3100_HSHAKE_VALUE) {
        return SensorI2CError;
    }

    if (RM3100ReadReg(RM3100_LROSCADJ_REG, &init_values[2], 2) < SensorOK) {
        return SensorErrorNonExistant;
    }

    if ((init_values[2] != RM3100_LROSCADJ_VALUE) || (init_values[3] != RM3100_SLPOSCADJ_VALUE)) {
        return SensorErrorUnexpectedDevice;
    }

    mag_change_cycle_count(initialCC);
    RM3100ReadReg(RM3100_CCX1_REG, &cycle_values[0], 1);
    RM3100ReadReg(RM3100_CCX0_REG, &cycle_values[1], 1);

    mCycleCount = cycle_values[0];
    mCycleCount = (mCycleCount << 8) | cycle_values[1];

    if (!singleMode) {
        mag_set_power_mode(SensorPowerModeContinuous);
        mag_set_sample_rate(sampleRate);
    } else {
        mag_set_power_mode(SensorPowerModeSingle);
    }

    return SensorOK;
}

uint8_t RM3100ByteReadReg(uint8_t addr) {
    uint8_t readBuf[1] = {0};
    RM3100ReadReg(addr, readBuf, 1);

    return readBuf[0];
}

int32_t RM3100ReadReg(uint8_t addr, uint8_t *readBuf, uint16_t size) {
    uint8_t writeBuf[1] = {addr};
    int32_t rv;

    if ((rv = io_write(rm3100_io, writeBuf, 1)) < 0) {
        warning("Error in RM3100 Write");
    }
    if ((rv = io_read(rm3100_io, readBuf, size)) < 0) {
        warning("Error in RM3100 Read");
    }
    return rv;
}

void RM3100GatherData(float *storeAddress) {
    uint8_t data[] = {REQUEST};
    RM3100WriteReg(RM3100_POLL_REG, data, 1);

    storeAddress[0] = mXYZ[0];
    storeAddress[1] = mXYZ[1];
    storeAddress[2] = mXYZ[2];

    return;
}

int32_t RM3100WriteReg(uint8_t addr, uint8_t *data, uint16_t size) {
    uint8_t writeBuf[MAX_I2C_WRITE + 1];
    int32_t rv;

    writeBuf[0] = addr;
    memcpy(&(writeBuf[1]), data, size);
    if ((rv = io_write(rm3100_io, writeBuf, size + 1)) < 0) {
        warning("Error in RM3100 Write");
    }
    return rv;
}

RM3100_return_t mag_read_data() {
    RM3100_return_t returnVals;

    // read out sensor data
    RM3100ReadReg(RM3100_QX2_REG, (uint8_t *)&mSamples, sizeof(mSamples) / sizeof(char));

    returnVals.x = ((int8_t)mSamples[0]) * 256 * 256;
    returnVals.x |= mSamples[1] * 256;
    returnVals.x |= mSamples[2];

    returnVals.y = ((int8_t)mSamples[3]) * 256 * 256;
    returnVals.y |= mSamples[4] * 256;
    returnVals.y |= mSamples[5];

    returnVals.z = ((int8_t)mSamples[6]) * 256 * 256;
    returnVals.z |= mSamples[7] * 256;
    returnVals.z |= mSamples[8];

    // stow sensor data
    mXYZ[0] = (float)returnVals.x / mGain;
    mXYZ[1] = (float)returnVals.y / mGain;
    mXYZ[2] = (float)returnVals.z / mGain;

    return returnVals;
}

SensorStatus mag_modify_interrupts(uint8_t cmm_value, uint8_t poll_value) {
    uint8_t data[2] = {cmm_value, poll_value};

    RM3100WriteReg(RM3100_CMM_REG, &data[0], 1);
    RM3100WriteReg(RM3100_POLL_REG, &data[1], 1);
    return SensorOK;
}

SensorPowerMode mag_set_power_mode(SensorPowerMode mode) {
    switch (mode) {
        case SensorPowerModeInactive:
            mSensorMode = mode;
            mag_modify_interrupts(RM3100_DISABLED, RM3100_DISABLED);
            break;
        case SensorPowerModeContinuous:
            mag_modify_interrupts(RM3100_ENABLED, RM3100_DISABLED);
            break;
        case SensorPowerModeSingle:
            mag_modify_interrupts(RM3100_DISABLED, RM3100_SINGLE);
            break;
    }

    mSensorMode = mode;
    return mSensorMode;
}

uint16_t mag_set_sample_rate(uint16_t sample_rate) {
    uint64_t i;
    uint8_t i2cbuffer[1];
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

    if (mSensorMode == SensorPowerModeContinuous) {
        mag_modify_interrupts(RM3100_DISABLED, RM3100_DISABLED);
    }

    mSampleRate = supported_rates[i][0];
    i2cbuffer[0] = (uint8_t)supported_rates[i][1];

    RM3100WriteReg(RM3100_TMRC_REG, i2cbuffer, 1);

    if (mSensorMode == SensorPowerModeContinuous) {
        mag_modify_interrupts(RM3100_ENABLED, RM3100_DISABLED);
    }

    RM3100ReadReg(RM3100_TMRC_REG, i2cbuffer, 1);

    return i2cbuffer[0];
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
    RM3100WriteReg(RM3100_CCX1_REG, settings, 6);
}

void exec_command_magnetometer(const command_t *const p_cmd) {
    // TODO
}

QueueHandle_t init_magnetometer(void) {
    // TODO: call hardware init from here

    // Initialize the magnetometer command queue
    QueueHandle_t magnetometer_command_queue_handle =
        xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, magnetometer_mem.magnetometer_command_queue_buffer,
                           &magnetometer_mem.magnetometer_task_queue);
    if (magnetometer_command_queue_handle == NULL) {
        fatal("Failed to create magnetometer queue!\n");
    }

    return magnetometer_command_queue_handle;
}

