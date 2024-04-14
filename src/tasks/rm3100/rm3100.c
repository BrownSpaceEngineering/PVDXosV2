/**
 * Code for the RM3100 Magnetometer Sensor
 * 
 * Created: Dec 7, 2023 2:22 AM
 * By: Nathan Kim
*/
#include "rm3100.h"
#include "logging.h"

//Io descriptor for the RM3100
struct io_descriptor *rm3100_io;
int32_t RM3100ReadReg(uint8_t addr, uint8_t *val);
int32_t RM3100WriteReg(uint8_t addr, uint8_t data);

//https://github.com/inventorandy/atmel-samd21/blob/master/07_I2CTSYS/07_I2CTSYS/ext_tsys01.h#L15

int init_rm3100(void) {
    //Initialize the I2C Communications
    i2c_m_sync_get_io_descriptor(&I2C_0, &rm3100_io);
    i2c_m_sync_enable(&I2C_0);
    i2c_m_sync_set_slaveaddr(&I2C_0, RM3100Address, I2C_M_SEVEN);

    //TODO Check REVID
    uint8_t revid;
    uint32_t error = RM3100ReadReg(RM3100_REVID_REG, &revid);
    if (revid != 0x22) {
        warning("RM3100 not detected correctly! Errored with code: %ld\n", error);
        return -1;
    }

    changeCycleCount(initialCC);

    if (singleMode) {
        RM3100WriteReg(RM3100_CMM_REG, 0);
        RM3100WriteReg(RM3100_POLL_REG, 0x70);
    } else {
        RM3100WriteReg(RM3100_CMM_REG, 0x79);
    }
    return 0;
}

void rm3100_main(void *pvParameters) {
    info("RM3100 Task Started!\r\n");

    init_rm3100();
    while(1) {
        values_loop();
    }
}

RM3100_return_t values_loop() {
    RM3100_return_t returnVals;

    if (useDRDYPin) {
        while(gpio_get_pin_level(DRDY_PIN) == 0) {
            delay_ms(100);
        }
    }

    uint8_t x2,x1,x0,y2,y1,y0,z2,z1,z0;

    RM3100ReadReg(RM3100_MX2_REG, &x2);
    RM3100ReadReg(RM3100_MX1_REG, &x1);
    RM3100ReadReg(RM3100_MX0_REG, &x0);
    RM3100ReadReg(RM3100_MY2_REG, &y2);
    RM3100ReadReg(RM3100_MY1_REG, &y1);
    RM3100ReadReg(RM3100_MY0_REG, &y0);
    RM3100ReadReg(RM3100_MZ2_REG, &z2);
    RM3100ReadReg(RM3100_MZ1_REG, &z1);
    RM3100ReadReg(RM3100_MZ0_REG, &z0);

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

    //Todo change this to do it 1 by 1
    uint32_t returnVal = RM3100WriteReg(RM3100_CCX1_REG, CCMSB);
    returnVal = RM3100WriteReg(RM3100_CCX0_REG, CCLSB);
    returnVal = RM3100WriteReg(RM3100_CCY1_REG, CCMSB);
    returnVal = RM3100WriteReg(RM3100_CCY0_REG, CCLSB);
    returnVal = RM3100WriteReg(RM3100_CCZ1_REG, CCMSB);
    returnVal = RM3100WriteReg(RM3100_CCZ0_REG, CCLSB);

    if ((int32_t) returnVal < 0) {
        debug("It errored\n");
    }
}

int32_t RM3100ReadReg(uint8_t addr, uint8_t *val) {
    static uint8_t writeBuf[1];
    writeBuf[0] = addr;
    int32_t rv;
    if ((rv = io_write(rm3100_io, writeBuf, 1)) != 0){
        warning("Error in RM3100 Write");
    } else {
        if ((rv = io_read(rm3100_io, val, 1)) != 0) {
            warning("Error in RM3100 Write");
        }
    }
    return rv;
}

int32_t RM3100WriteReg(uint8_t addr, uint8_t data) {
    uint8_t writeBuf1[2] = {addr, data};

    io_write(rm3100_io, writeBuf1, 1);
    return io_write(rm3100_io, writeBuf1 + 1, 1);
}





