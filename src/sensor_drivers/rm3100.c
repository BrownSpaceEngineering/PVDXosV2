/**
 * Code for the RM3100 Magnetometer Sensor
 * 
 * Created: Dec 7, 2023 2:22 AM
 * By: Nathan Kim
*/
#include "rm3100.h"

//Io descriptor for the RM3100
struct io_descriptor *rm3100_io;

//https://github.com/inventorandy/atmel-samd21/blob/master/07_I2CTSYS/07_I2CTSYS/ext_tsys01.h#L15

void init_rm3100(void) {
    //Initialize the I2C Communications
    i2c_m_sync_get_io_descriptor(&I2C_0, &rm3100_io);
    i2c_m_sync_enable(&I2C_0);
    i2c_m_sync_set_slaveaddr(&I2C_0, RM3100Address, I2C_M_SEVEN);

    changeCycleCount(initialCC);

    if (singleMode) {
        uint8_t buf1[1] = { RM3100_CMM_REG };
        uint8_t buf2[1] = { RM3100_POLL_REG }; 
        io_write(rm3100_io, buf1, 0);
        io_write(rm3100_io, buf2, 0x70);
    } else {
        uint8_t buf1[1] = { RM3100_CMM_REG };
        io_write(rm3100_io, buf1, 0x79);
    }
}

void changeCycleCount(uint16_5 newCC) {
    
}





