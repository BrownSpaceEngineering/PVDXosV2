/**
 * Code for the RM3100 Magnetometer Sensor
 * 
 * Created: Dec 7, 2023 2:22 AM
 * By: Nathan Kim
*/
#include "rm3100.h"

//https://github.com/inventorandy/atmel-samd21/blob/master/07_I2CTSYS/07_I2CTSYS/ext_tsys01.h#L15

void init_rm3100(void) {
    //Initialize the I2C Communications
    i2c_m_sync_get_io_descriptor(&RM3100, &rm3100_io);
    i2c_m_sync_enable(&RM3100);
    i2c_m_sync_set_slaveaddr(&RM3100, 0x77, I2C_M_SEVEN);
}