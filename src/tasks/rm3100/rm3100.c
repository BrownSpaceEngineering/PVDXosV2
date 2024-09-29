/**
 * Code for the RM3100 Magnetometer Sensor
 * 
 * Created: Dec 7, 2023 2:22 AM
 * By: Nathan Kim
*/
#include "rm3100.h"
#include "logging.h"

#define I2C_SERCOM       SERCOM5
//Io descriptor for the RM3100

struct rm3100TaskMemory rm3100Mem;

struct io_descriptor *rm3100_io;

static unsigned short int           mSampleRate;
static SensorPowerMode              mSensorMode;
static char                         mSamples[9];

int32_t RM3100ReadReg(uint8_t addr, uint8_t *val, uint16_t size);
int32_t RM3100WriteReg(uint8_t addr, uint8_t *data, uint16_t size);

//https://github.com/inventorandy/atmel-samd21/blob/master/07_I2CTSYS/07_I2CTSYS/ext_tsys01.h#L15
//https://os.mbed.com/users/ddelsuc/code/RM3100BB_Sample_Code/

int init_rm3100(void) {
    //Initialize the I2C Communications
    i2c_m_sync_set_baudrate(&I2C_0, 0, 115200);
    i2c_m_sync_get_io_descriptor(&I2C_0, &rm3100_io);
    i2c_m_sync_enable(&I2C_0);
    i2c_m_sync_set_slaveaddr(&I2C_0, RM3100Address, I2C_M_SEVEN);

    uint8_t i2cbuffer[2];
    uint8_t settings[7];
     
    if(RM3100ReadReg(RM3100_LROSCADJ_REG, i2cbuffer, 2) < 0)
    {
        return SensorErrorNonExistant;
    }

    if (    (i2cbuffer[0] != RM3100_LROSCADJ_VALUE) ||
            (i2cbuffer[1] != RM3100_SLPOSCADJ_VALUE))
    {
        return SensorErrorUnexpectedDevice;
    }

    /* Zero buffer content */
    i2cbuffer[0]=0; 
    i2cbuffer[1]=0;
    
    /* Clears MAG and BEACON register and any pending measurement */
    RM3100WriteReg(RM3100_MAG_REG, i2cbuffer, 2);
    
    /* Initialize settings */
    settings[0]=CCP1; /* CCPX1 */
    settings[1]=CCP0; /* CCPX0 */
    settings[2]=CCP1; /* CCPY1 */
    settings[3]=CCP0; /* CCPY0 */
    settings[4]=CCP1; /* CCPZ1 */
    settings[5]=CCP0; /* CCPZ0 */
    settings[6]=NOS;
    /* settings[7]=TMRC;  */
    
    /*  Write register settings */
    RM3100WriteReg(RM3100_CCPX1_REG, settings, 7);
    
    mag_set_power_mode(SensorPowerModePowerDown);
        
    return SensorOK;
}

void rm3100_main(void *pvParameters) {
    info("RM3100 Task Started!\r\n");

    int setup = init_rm3100();

    mag_set_sample_rate(100); //100Hz

	mag_set_power_mode(SensorPowerModeActive);

    int CycleCount = CCP0 | (CCP1 << 8);
	float gain = 0.3671 * CycleCount + 1.5;
    watchdog_checkin(RM3100_TASK);

    while(1) {
        if (setup == 0) {
            RM3100_return_t values = values_loop();

            uint32_t x = (float)values.x / gain;
            uint32_t y = (float)values.y / gain;
            uint32_t z = (float)values.z / gain;

            if (x == 0 && y == 0 && z == 0) {
                 vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        watchdog_checkin(RM3100_TASK);
    }
}

RM3100_return_t values_loop() {
    RM3100_return_t returnVals;

    RM3100ReadReg(RM3100_QX2_REG, (uint8_t *)&mSamples, sizeof(mSamples)/sizeof(char));
    
    returnVals.x = ((signed char)mSamples[0]) * 256 * 256;
    returnVals.x |= mSamples[1] * 256;
    returnVals.x |= mSamples[2];

    returnVals.y = ((signed char)mSamples[3]) * 256 * 256;
    returnVals.y |= mSamples[4] * 256;
    returnVals.y |= mSamples[5];

    returnVals.z = ((signed char)mSamples[6]) * 256 * 256;
    returnVals.z |= mSamples[7] * 256;
    returnVals.z |= mSamples[8];

    return returnVals;
}

RM3100_bist_t bist_register_get() {
    RM3100_bist_t returnVals = { .bist = 0 };
    uint8_t zeroVal = 0;

    uint8_t sendVal[2] = { 0x33, 0b11111111 };

    RM3100WriteReg(RM3100_BEACON_REG, &zeroVal, 1);

    RM3100WriteReg(RM3100_BIST_REG, sendVal, 2);
    RM3100WriteReg(RM3100_POLL_REG, &zeroVal, 1);

    while(gpio_get_pin_level(DRDY_PIN) == 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        watchdog_checkin(RM3100_TASK);
    }

    return returnVals;
}

int32_t RM3100ReadReg(uint8_t addr, uint8_t *val, uint16_t size) {
    i2c_m_sync_get_io_descriptor(&I2C_0, &rm3100_io);
    uint8_t writeBuf[1] = { addr };
    int32_t rv;
    if ((rv = io_write(rm3100_io, writeBuf, 1)) < 0){
        warning("Error in RM3100 Write");
    }
    //vTaskDelay(pdMS_TO_TICKS(20));
    if ((rv = io_read(rm3100_io, val, size)) < 0) {
        warning("Error in RM3100 Write");
    }
    return rv;
}

int32_t RM3100WriteReg(uint8_t addr, uint8_t *data, uint16_t size) {
    // uint8_t writeBuf1[2] = {addr, data};
    i2c_m_sync_get_io_descriptor(&I2C_0, &rm3100_io);
    io_write(rm3100_io, &addr, 1);
    //vTaskDelay(pdMS_TO_TICKS(20));
    return io_write(rm3100_io, data, size);
}

SensorStatus mag_enable_interrupts()
{
    static uint8_t data[] = { RM3100_ENABLED };

    if (mSensorMode == SensorPowerModeActive) 
    {
        RM3100WriteReg(RM3100_BEACON_REG, data, sizeof(data)/sizeof(char));
    }
    return SensorOK;
}

SensorStatus mag_disable_interrupts()
{
    static uint8_t data[] = { RM3100_DISABLED };
    RM3100WriteReg(RM3100_BEACON_REG, data, sizeof(data)/sizeof(char));
    return SensorOK;
}

SensorPowerMode mag_set_power_mode(SensorPowerMode mode)
{
    switch(mode)
    {
        default:
            return mSensorMode;
            
        case SensorPowerModePowerDown:
        case SensorPowerModeSuspend:
            mSensorMode = mode;
            mag_disable_interrupts();
            break;

        case SensorPowerModeActive:
            mSensorMode = SensorPowerModeActive;
            mag_enable_interrupts();
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