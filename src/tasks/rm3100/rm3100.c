/**
 * Code for the RM3100 Magnetometer Sensor
 * 
 * Created: Dec 7, 2023 2:22 AM
 * By: Nathan Kim, Alexander Thaep
*/
#include "rm3100.h"
#include "string.h"
#include "logging.h"

#define I2C_SERCOM       SERCOM6
//Io descriptor for the RM3100

struct rm3100TaskMemory rm3100Mem;

struct io_descriptor *rm3100_io;

static uint16_t                     mSampleRate;
static int8_t                       mSamples[9];
static SensorPowerMode              mSensorMode;

static uint8_t                      cycleCount;

int32_t RM3100ReadReg(uint8_t addr, uint8_t *val, uint16_t size);
int32_t RM3100WriteReg(uint8_t addr, uint8_t *data, uint16_t size);

//https://github.com/inventorandy/atmel-samd21/blob/master/07_I2CTSYS/07_I2CTSYS/ext_tsys01.h#L15
//https://os.mbed.com/users/ddelsuc/code/RM3100BB_Sample_Code/

// For future reference, PIN 51 goes to SCL and PIN 52 goes to SDA

int init_rm3100(void) {
    //Initialize the I2C Communications
    i2c_m_sync_set_baudrate(&I2C_0, 0, 115200);
    i2c_m_sync_get_io_descriptor(&I2C_0, &rm3100_io);
    i2c_m_sync_enable(&I2C_0);
    i2c_m_sync_set_slaveaddr(&I2C_0, RM3100Address, I2C_M_SEVEN);

    uint8_t i2cbuffer[2];

    // if(RM3100ReadReg(RM3100_LROSCADJ_REG, i2cbuffer, 2) < SensorOK)
    // {
    //     return SensorErrorNonExistant;
    // }

    // if (    (i2cbuffer[0] != RM3100_LROSCADJ_VALUE) ||
    //         (i2cbuffer[1] != RM3100_SLPOSCADJ_VALUE))
    // {
    //     return SensorErrorUnexpectedDevice;
    // }

    /* Zero buffer content */
    i2cbuffer[0]=0; 
    i2cbuffer[1]=0;

    // mag_set_sample_rate(100); //100Hz

    changeCycleCount(initialCC);
    RM3100ReadReg(RM3100_CCX1_REG, i2cbuffer, 1);
    RM3100ReadReg(RM3100_CCX0_REG, i2cbuffer + 1, 1);

    cycleCount = i2cbuffer[0];
    cycleCount = (cycleCount << 8) | i2cbuffer[1];

    if (!singleMode)
    {
        mag_set_power_mode(SensorPowerModeActive);
    }
    else
    {
        mag_set_power_mode(SensorPowerModeSingle);
    }

    return SensorOK;
}

void rm3100_main(void *pvParameters) {
    info("RM3100 Task Started!\r\n");

    int setup = init_rm3100();

    if (setup > 0)
    {
        return;
    }

    watchdog_checkin(RM3100_TASK);

    float gain = 0.3671 * cycleCount + 1.5;

    if (!singleMode)
    {
        while(1) {
            while(gpio_get_pin_level(DRDY_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(100));
                watchdog_checkin(RM3100_TASK);
            }

            RM3100_return_t values = values_loop();

            float x = (float)values.x / gain;
            float y = (float)values.y / gain;
            float z = (float)values.z / gain;

            info("X: %f, Y: %f, Z: %f", x, y, z);
            watchdog_checkin(RM3100_TASK);
        }
    }
    else
    {
        while(1)
        {
            static uint8_t data[] = { REQUEST }; 
            RM3100WriteReg(RM3100_POLL_REG, data, 1);

            while(gpio_get_pin_level(DRDY_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(100));
                watchdog_checkin(RM3100_TASK);
            }

            RM3100_return_t values = values_loop();

            float x = (float)values.x / gain;
            float y = (float)values.y / gain;
            float z = (float)values.z / gain;

            info("X: %f, Y: %f, Z: %f", x, y, z);
            watchdog_checkin(RM3100_TASK);
        }  
    }
}

RM3100_return_t values_loop() {
    RM3100_return_t returnVals;

    // read out sensor data
    RM3100ReadReg(RM3100_QX2_REG, (uint8_t*) &mSamples, sizeof(mSamples)/sizeof(char));
    
    returnVals.x = ((int8_t)mSamples[0]) * 256 * 256;
    returnVals.x |= mSamples[1] * 256;
    returnVals.x |= mSamples[2];

    returnVals.y = ((int8_t)mSamples[3]) * 256 * 256;
    returnVals.y |= mSamples[4] * 256;
    returnVals.y |= mSamples[5];

    returnVals.z = ((int8_t)mSamples[6]) * 256 * 256;
    returnVals.z |= mSamples[7] * 256;
    returnVals.z |= mSamples[8];

    return returnVals;
}

uint8_t mag_get_hshake() {
    uint8_t readBuf[1] = { 0 };
    RM3100ReadReg(RM3100_HSHAKE_REG, readBuf, 1);

    return readBuf[0];
}

uint8_t mag_get_status() {
    uint8_t readBuf[1] = { 0 };
    uint8_t writeBuf[1] = { RM3100_STATUS_VALUE };
    RM3100WriteReg(RM3100_STATUS_REG, writeBuf, 1);
    RM3100ReadReg(RM3100_STATUS_REG, readBuf, 1);

    return readBuf[0];
}

uint8_t mag_get_revid() {
    // We are on revision 34 (decimal)
    uint8_t readBuf[1] = { 0 };
    uint8_t writeBuf[1] = { RM3100_REVID_VALUE };
    RM3100WriteReg(RM3100_REVID_REG, writeBuf, 1);
    RM3100ReadReg(RM3100_REVID_REG, readBuf, 1);

    return readBuf[0];
}

int32_t RM3100ReadReg(uint8_t addr, uint8_t *readBuf, uint16_t size) {
    uint8_t writeBuf[1] = { addr };
    int32_t rv;
    if ((rv = io_write(rm3100_io, writeBuf, 1)) < 0){
        warning("Error in RM3100 Write");
    }
    if ((rv = io_read(rm3100_io, readBuf, size)) < 0) {
        warning("Error in RM3100 Read");
    }
    return rv;
}

int32_t RM3100WriteReg(uint8_t addr, uint8_t *data, uint16_t size) {
    uint8_t writeBuf[MAX_I2C_WRITE + 1];
    writeBuf[0] = addr;
    memcpy(&(writeBuf[1]), data, size);
    int32_t rv;
    if ((rv = io_write(rm3100_io, writeBuf, size + 1)) < 0){
        warning("Error in RM3100 Write");
    }
    return rv;
}

SensorStatus mag_enable_single()
{
    uint8_t data_0[] = { 0 };
    uint8_t data_1[] = { RM3100_SINGLE };

    RM3100WriteReg(RM3100_CMM_REG, data_0, 1);
    RM3100WriteReg(RM3100_POLL_REG, data_1, 1);
    return SensorOK;
}

SensorStatus mag_enable_interrupts()
{
    uint8_t data[] = { RM3100_ENABLED };

    RM3100WriteReg(RM3100_CMM_REG, data, 1);
    return SensorOK;
}

SensorStatus mag_disable_interrupts()
{
    uint8_t data[] = { RM3100_DISABLED };

    RM3100WriteReg(RM3100_CMM_REG, data, 1);
    RM3100WriteReg(RM3100_POLL_REG, data, 1);
    return SensorOK;
}

SensorPowerMode mag_set_power_mode(SensorPowerMode mode)
{
    switch(mode)
    {
        default:
            return mSensorMode;
        case SensorPowerModePowerDown:
            mSensorMode = mode;
            mag_disable_interrupts();
            break;
        case SensorPowerModeActive:
            mSensorMode = mode;
            mag_enable_interrupts();
            break;
        case SensorPowerModeSingle:
            mSensorMode = mode;
            mag_enable_single();
            break;
    }

    return mSensorMode;
}

unsigned short mag_set_sample_rate(unsigned short sample_rate)
{
    uint64_t i;
    static uint8_t i2cbuffer[1];
    const unsigned short int supported_rates[][2] = \
    {
        /* [Hz], register value */
        {   2, 0x0A},   // up to 2Hz
        {   4, 0x09},   // up to 4Hz
        {   8, 0x08},   // up to 8Hz
        {  16, 0x07},   // up to 16Hz
        {  31, 0x06},   // up to 31Hz
        {  62, 0x05},   // up to 62Hz
        {  125, 0x04},  // up to 125Hz
        {  220, 0x03}   // up to 250Hz
        };
        
    for(i = 0; i < sizeof(supported_rates)/(sizeof(unsigned short int)*2) - 1; i++)
    {
        if(sample_rate <= supported_rates[i][0]) break;
    }
            
    if (mSensorMode == SensorPowerModeActive) 
    {
        mag_disable_interrupts();
    }
    
    mSampleRate = supported_rates[i][0];
    i2cbuffer[0]= (uint8_t)supported_rates[i][1];
  
    RM3100WriteReg(RM3100_TMRC_REG, i2cbuffer, 1);
    
    if (mSensorMode == SensorPowerModeActive) 
    {
        mag_enable_interrupts();
    }

    return mSampleRate;

}

void changeCycleCount(uint16_t newCC){
    uint8_t settings[6];

    uint8_t CCMSB = (newCC & 0xFF00) >> 8; //get the most significant byte
    uint8_t CCLSB = newCC & 0xFF; //get the least significant byte

    /* Initialize settings */
    settings[0]=CCMSB; /* CCPX1 */
    settings[1]=CCLSB; /* CCPX0 */
    settings[2]=CCMSB; /* CCPY1 */
    settings[3]=CCLSB; /* CCPY0 */
    settings[4]=CCMSB; /* CCPZ1 */
    settings[5]=CCLSB; /* CCPZ0 */

    /*  Write register settings */
    RM3100WriteReg(RM3100_CCPX1_REG, settings, 6);

    /*
    Wire.beginTransmission(RM3100Address);
    Wire.write(RM3100_CCX1_REG);
    Wire.write(CCMSB);  //write new cycle count to ccx1
    Wire.write(CCLSB);  //write new cycle count to ccx0
    Wire.write(CCMSB);  //write new cycle count to ccy1
    Wire.write(CCLSB);  //write new cycle count to ccy0
    Wire.write(CCMSB);  //write new cycle count to ccz1
    Wire.write(CCLSB);  //write new cycle count to ccz0     
    Wire.endTransmission(); 
    */ 
}