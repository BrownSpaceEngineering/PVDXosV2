#ifndef ARDUCAM_H_
#define ARDUCAM_H_

#include <atmel_start.h>
#include <driver_init.h>
#include "globals.h"
#include "rtos_start.h"
#include "watchdog_task.h"

#define ARDUCAM_TASK_STACK_SIZE 256

#define ARDUCAMAddress          0x60

#define OV2640_CHIPID_HIGH 	    0x0A
#define OV2640_CHIPID_LOW 	    0x0B

#define ARDUCHIP_FIFO           0x04
#define FIFO_CLEAR_MASK    		0x01
#define FIFO_START_MASK    		0x02

#define ARDUCHIP_TEST1          0x00 // TEST REGISTER

#define ARDUCAM_SPI_BUFFER_CAPACITY 64

// Functions for setting the chip-select pins on the camera to low/high voltage
#define CS_LOW() gpio_set_pin_level(Display_CS, 0)
#define CS_HIGH() gpio_set_pin_level(Display_CS, 1)

struct sensor_reg {
	uint8_t reg;
	uint8_t val;
};

// Buffer for SPI transactions
extern uint8_t ardu_spi_rx_buffer[ARDUCAM_SPI_BUFFER_CAPACITY];
extern uint8_t ardu_spi_tx_buffer[ARDUCAM_SPI_BUFFER_CAPACITY];
extern struct spi_xfer ardu_xfer;

struct arducamTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t arducamTaskStack[ARDUCAM_TASK_STACK_SIZE];
    StaticTask_t arducamTaskTCB;
};

extern struct arducamTaskMemory arducamMem;

void arducam_main(void *pvParameters);
void init_arducam();

uint32_t ARDUCAMI2CWrite(uint8_t addr, uint8_t *data, uint16_t size);
uint32_t ARDUCAMI2CRead(uint8_t addr, uint8_t *readBuf, uint16_t size);
uint32_t wrSensorRegs8_8(const struct sensor_reg reglist[]);
int32_t arducam_spi_write_command();
int32_t ARDUCAMwReg(uint8_t, uint8_t);
int8_t ARDUCAMrReg(uint8_t);

#endif // arducam_h_