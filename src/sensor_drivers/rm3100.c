/**
 * Code for the RM3100 Magnetometer Sensor
 * 
 * Created: Dec 7, 2023 2:22 AM
 * By: Nathan Kim
*/
#include "rm3100.h"

//Io descriptor for the RM3100
struct io_descriptor *rm3100_io;
uint32_t RM3100ReadReg(uint8_t addr, uint8_t buf, uint8_t size);
uint32_t RM3100WriteReg(uint8_t addr, uint8_t data);

//https://github.com/inventorandy/atmel-samd21/blob/master/07_I2CTSYS/07_I2CTSYS/ext_tsys01.h#L15

void init_rm3100(void) {
    //Initialize the I2C Communications
    i2c_m_sync_get_io_descriptor(&I2C_0, &rm3100_io);
    i2c_m_sync_enable(&I2C_0);
    i2c_m_sync_set_slaveaddr(&I2C_0, RM3100Address, I2C_M_SEVEN);

    changeCycleCount(initialCC);

    if (singleMode) {
        RM3100WriteReg(RM3100_CMM_REG, 0);
        RM3100WriteReg(RM3100_POLL_REG, 0x70);
    } else {
        RM3100WriteReg(RM3100_CMM_REG, 0x79);
    }
}

RM3100_return_t values_loop(void) {
    RM3100_return_t returnVals;

    if (useDRDYPin) {
        while(gpio_get_pin_level(DRDY_PIN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    } else {
        //TO DO: WE HAVE NO IDEA HOW PINS WORK (STILL)
    }

    uint8_t readBuf[9] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t x2,x1,x0,y2,y1,y0,z2,z1,z0;
    uint32_t val = RM3100ReadReg(RM3100_MX2_REG, readBuf, 9);

    if (val < 0) {
        printf("We have an error with write in RM3100\n");
    }
    x2 = readBuf[0];
    x1 = readBuf[1];
    x0 = readBuf[2];
    y2 = readBuf[3];
    y1 = readBuf[4];
    y0 = readBuf[5];
    z2 = readBuf[6];
    z1 = readBuf[7];
    z0 = readBuf[8];

    //Weird bit manipulation
    if (x2 & 0x80) {
        returnVals.x = 0xFF;
    }
    if (y2 & 0x80) {
        returnVals.y = 0xFF;
    }
    if (z2 & 0x80) {
        returnVals.z = 0xFF;
    }

    returnVals.x = (returnVals.x * 256 * 256 * 256) | (int32_t)(x2) * 256 * 256 | (uint16_t)(x1) * 256 | x0;
    returnVals.y = (returnVals.y * 256 * 256 * 256) | (int32_t)(y2) * 256 * 256 | (uint16_t)(y1) * 256 | y0;
    returnVals.z = (returnVals.z * 256 * 256 * 256) | (int32_t)(z2) * 256 * 256 | (uint16_t)(z1) * 256 | z0;

    return returnVals;
}

void changeCycleCount(uint16_t newCC) {
    // Cycle count most sig byte
    uint8_t CCMSB = (newCC & 0xFF00) >> 8;
    // Cycle count least sig byte
    uint8_t CCLSB = newCC & 0xFF;

    uint8_t buf[7] = { RM3100_CCX1_REG, CCMSB, CCLSB, CCMSB, CCLSB, CCMSB, CCLSB };
    io_write(rm3100_io, buf, 7);
}

uint32_t RM3100ReadReg(uint8_t addr, uint8_t buf, uint8_t size) {
    uint8_t writeBuf[1] = {addr};
    io_write(rm3100_io, writeBuf, 1);

    return io_read(rm3100_io, buf, size);
}

uint32_t RM3100WriteReg(uint8_t addr, uint8_t data) {
    uint8_t writeBuf1[1] = {addr};
    uint8_t writeBuf2[1] = {data};

    io_write(rm3100_io, writeBuf1, 1);
    return io_write(rm3100_io, writeBuf2, 1);
}





