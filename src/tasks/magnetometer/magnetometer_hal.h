#ifndef MAGNETOMETER_HAL_H
#define MAGNETOMETER_HAL_H

#include <stdint.h>

#define MAX_I2C_WRITE                32

// We are on revision 34 (decimal), 0x22 (hex)

// Need to put the holy grail of values here
#define RM3100_ADDRESS 		0x20 // Hexadecimal slave address for RM3100 with Pin 2 and Pin 4 set to LOW

// Data reading regs are numbered in the opposite from documentation so we're reading 0-1-2 rather than 2-1-0 cause we hate RM3100
//internal register values without the R/W bit
#define RM3100_REVID_REG 	0x36 // Hexadecimal address for the RevID internal register
#define RM3100_POLL_REG 	0x00 // Hexadecimal address for the Poll internal register
#define RM3100_CMM_REG 		0x01 // Hexadecimal address for the CMM internal register
#define RM3100_STATUS_REG 	0x34 // Hexadecimal address for the Status internal register
#define RM3100_HSHAKE_REG	0x35 // Hexadecimal address for the HSHAKE internal register
#define RM3100_CCX1_REG 	0x04 // Hexadecimal address for Cycle Count X1 internal register
#define RM3100_CCX0_REG 	0x05 // Hexadecimal address for the Cycle Count X0 internal register
#define RM3100_CCY1_REG 	0x06
#define RM3100_CCY0_REG 	0x07
#define RM3100_CCZ1_REG 	0x08
#define RM3100_CCZ0_REG		0x09
#define RM3100_QX2_REG		0x24
#define RM3100_QX1_REG		0x25
#define RM3100_QX0_REG		0x26
#define RM3100_QY2_REG		0x27
#define RM3100_QY1_REG		0x28
#define RM3100_QY0_REG		0x29
#define RM3100_QZ2_REG		0x2A
#define RM3100_QZ1_REG		0x2B
#define RM3100_QZ0_REG		0x2C
#define RM3100_TMRC_REG             0x0B

#define RM3100_TEST3_REG            0x72

#define RM3100_LROSCADJ_REG         0x63

#define RM3100_LROSCADJ_VALUE       0xA7
#define RM3100_SLPOSCADJ_VALUE      0x08 

#define RM3100_REVID_VALUE          0x22 
#define RM3100_HSHAKE_VALUE			0x1B

#define RM3100_ENABLED              0x79
#define RM3100_SINGLE				0x70
#define RM3100_DISABLED             0x00

#define RM3100_BIST_REG				0x33

#define RM3100_PNI_KEY1_REG			0x2D
#define RM3100_PNI_KEY2_REG			0x2E

#define RM3100_NOS_REG				0x0A

#define CCP0	0xC8	/* 200 Cycle Count */
#define CCP1	0x00
#define NOS		0x01	/* Number of Samples for averaging */

#define REQUEST 0x70 // 0b 0111 0000

// options
#define INITIAL_CC  200 // Set the cycle count
#define SAMPLE_RATE   2 // 2 HZ
#define SINGLE_MODE   0 // 0 = use continuous measurement mode; 1 = use single measurement mode

typedef struct {
    int32_t x;
    int32_t y;
    int32_t z;
} rm3100_return_t;

typedef enum {
	SENSOR_POWER_MODE_INACTIVE = 0,       
	SENSOR_POWER_MODE_CONTINUOUS = 1,  
	SENSOR_POWER_MODE_SINGLE = 2
} rm3100_power_mode_t;

#endif // MAGNETOMETER_HAL_H