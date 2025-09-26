#ifndef ARDUCAM_H_
#define ARDUCAM_H_

#include <atmel_start.h>
#include <driver_init.h>
#include "globals.h"
#include "rtos_start.h"
#include "watchdog_task.h"

#define ARDUCAM_TASK_STACK_SIZE 256

#define ARDUCAM_ADDR            0x60
#define SPI_ARDUCAM             0x00

#define OV2640_CHIPID_HIGH 	    0x0A
#define OV2640_CHIPID_LOW 	    0x0B

#define ARDUCHIP_FIFO           0x04
#define FIFO_CLEAR_MASK    		0x01
#define FIFO_START_MASK    		0x02

#define FIFO_SIZE1				0x42  //Camera write FIFO size[7:0] for burst to read
#define FIFO_SIZE2				0x43  //Camera write FIFO size[15:8]
#define FIFO_SIZE3				0x44  //Camera write FIFO size[18:16]

#define BURST_FIFO_READ			0x3C  //Burst FIFO read operation
#define SINGLE_FIFO_READ		0x3D  //Single FIFO read operation

#define ARDUCHIP_TEST1          0x00 // TEST REGISTER

#define ARDUCHIP_TRIG      		0x41
#define VSYNC_MASK         		0x01
#define SHUTTER_MASK       		0x02
#define CAP_DONE_MASK      		0x08

#define ARDUCAM_SPI_RX_BUF_SIZE 0x1000
#define ARDUCAM_SPI_TX_BUF_SIZE 0x40
#define I2C_SERCOM       SERCOM6

// Functions for setting the chip-select pins on the camera to low/high voltage

extern struct io_descriptor *arducam_i2c_io;
extern struct io_descriptor *arducam_spi_io;
extern struct arducamTaskMemory arducamMem;

struct sensor_reg {
	uint8_t reg;
	uint8_t val;
};

// Buffer for SPI transactions
extern uint8_t ardu_spi_rx_buffer[ARDUCAM_SPI_RX_BUF_SIZE];
extern uint8_t ardu_spi_tx_buffer[ARDUCAM_SPI_TX_BUF_SIZE];
extern struct spi_xfer ardu_xfer;

struct arducamTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t arducamTaskStack[ARDUCAM_TASK_STACK_SIZE];
    StaticTask_t arducamTaskTCB;
};

void arducam_main(void *pvParameters);
void init_arducam();

uint32_t ARDUCAMI2CWrite(uint8_t addr, uint8_t *data, uint16_t size);
uint32_t ARDUCAMI2CMultiWrite(const struct sensor_reg reglist[]);
uint32_t ARDUCAMI2CRead(uint8_t addr, uint8_t *readBuf, uint16_t size);
int32_t ARDUCAMSPIWrite(uint8_t, uint8_t);
int8_t ARDUCAMSPIRead(uint8_t);
void capture(void);

#endif // arducam_h_