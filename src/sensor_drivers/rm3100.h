#ifndef RM3100_H_
#define RM3100_H_

#include "globals.h"

//Need to put the holy grail of values here
#define RM3100Address 0x20 // Hexadecimal slave address for RM3100 with Pin 2 and Pin 4 set to LOW

//pin definitions
#define PIN_DRDY 9 //Set pin D9 to be the Data Ready Pin

// Data reading regs are numbered in the opposite from documentation so we're reading 0-1-2 rather than 2-1-0 cause we hate RM3100
//internal register values without the R/W bit
#define RM3100_REVID_REG 0x36 // Hexadecimal address for the Revid internal register
#define RM3100_POLL_REG 0x00 // Hexadecimal address for the Poll internal register
#define RM3100_CMM_REG 0x01 // Hexadecimal address for the CMM internal register
#define RM3100_STATUS_REG 0x34 // Hexadecimal address for the Status internal register
#define RM3100_CCX1_REG 0x04 // Hexadecimal address for Cycle Count X1 internal register
#define RM3100_CCX0_REG 0x05 // Hexadecimal address for the Cycle Count X0 internal register
#define RM3100_CCY1_REG 0x06
#define RM3100_CCY0_REG 0x07
#define RM3100_CCZ1_REG 0x08
#define RM3100_CCZ0_REG 0x09
#define RM3100_MX2_REG 0x24
#define RM3100_MX1_REG 0x25 // Hexadecimal address for Cycle Count X1 internal register
#define RM3100_MX0_REG 0x26 // Hexadecimal address for the Cycle Count X0 internal register
#define RM3100_MY2_REG 0x27
#define RM3100_MY1_REG 0x28
#define RM3100_MY0_REG 0x29
#define RM3100_MZ2_REG 0x2A
#define RM3100_MZ1_REG 0x2B
#define RM3100_MZ0_REG 0x2C

//options
#define initialCC 200 // Set the cycle count
#define singleMode 0 //0 = use continuous measurement mode; 1 = use single measurement mode
#define useDRDYPin 1 //0 = not using DRDYPin ; 1 = using DRDYPin to wait for data

int init_rm3100(void);

typedef struct {
    long x;
    long y;
    long z;
} RM3100_return_t;

RM3100_return_t values_loop();

void changeCycleCount(uint16_t);

#endif // rm3100_h_