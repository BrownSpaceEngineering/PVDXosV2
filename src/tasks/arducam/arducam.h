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

#define ARDUCAM_SPI_BUFFER_CAPACITY 64

struct sensor_reg {
	uint8_t reg;
	uint8_t val;
};

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

#endif // arducam_h_