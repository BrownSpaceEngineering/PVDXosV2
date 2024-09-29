/*
 * OV2640_regs.h
 * Contains register values for various OV2640 configurations,
 *
 * Created: 12/29/21
 * Author: Brown Space Engineering
 */

#ifndef PVDX_ARDUCAM_DRIVER_OV2640_REGS_H
#define PVDX_ARDUCAM_DRIVER_OV2640_REGS_H

// Test register
#define ARDUCHIP_TEST1 0x00

// Chip identification constants
#define OV2640_CHIPID_HIGH 	0x0A
#define OV2640_CHIPID_LOW 	0x0B

// Model-specific sensor address
#define OV2640_I2C_ADDR 0x60

// FIFO constants
#define ARDUCHIP_FIFO      		0x04  //FIFO and I2C control
#define BURST_FIFO_READ			0x3C  //Burst FIFO read operation
#define SINGLE_FIFO_READ		0x3D  //Single FIFO read operation
#define FIFO_CLEAR_MASK    		0x01
#define FIFO_START_MASK    		0x02

// Constants for checking capture termination
#define ARDUCHIP_TRIG      		0x41  //Trigger source
#define CAP_DONE_MASK      		0x08

// FIFO buffer register constants
#define FIFO_SIZE1				0x42  //Camera write FIFO size[7:0] for burst to read
#define FIFO_SIZE2				0x43  //Camera write FIFO size[15:8]
#define FIFO_SIZE3				0x44  //Camera write FIFO size[18:16]

// Representation of register assignments
struct sensor_reg {
    unsigned int reg;
    unsigned int val;
};

// Initialization register configs

const struct sensor_reg OV2640_JPEG_INIT[191];

const struct sensor_reg OV2640_YUV422[10];

const struct sensor_reg OV2640_JPEG[9];

// Resolution configurations

const struct sensor_reg OV2640_160x120_JPEG[40];

const struct sensor_reg OV2640_320x240_JPEG[40];

const struct sensor_reg OV2640_640x480_JPEG[41];

const struct sensor_reg OV2640_1024x768_JPEG[39];

#endif //PVDX_ARDUCAM_DRIVER_OV2640_REGS_H
