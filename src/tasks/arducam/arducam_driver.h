#ifndef ARDUCAM_DRIVER_H
#define ARDUCAM_DRIVER_H

#include <stdint.h>
#include <atmel_start.h>
#include <driver_init.h>
#include "globals.h"
#include "rtos_start.h"

struct sensor_reg {
	uint8_t reg;
	uint8_t val;
};

// Hardware Defines
#define ARDUCAM_ADDR            0x60
#define SPI_ARDUCAM             0x00

// Arducam Defines
#define OV2640_CHIPID_HIGH 	    0x0A
#define OV2640_CHIPID_LOW 	    0x0B

#define ARDUCHIP_FIFO           0x04
#define FIFO_CLEAR_MASK    		0x01
#define FIFO_START_MASK    		0x02

#define FIFO_SIZE1				0x42  // Camera write FIFO size[7:0] for burst to read
#define FIFO_SIZE2				0x43  // Camera write FIFO size[15:8]
#define FIFO_SIZE3				0x44  // Camera write FIFO size[18:16]

#define BURST_FIFO_READ			0x3C  // Burst FIFO read operation
#define SINGLE_FIFO_READ		0x3D  // Single FIFO read operation

#define ARDUCHIP_TEST1          0x00  // TEST REGISTER

#define ARDUCHIP_TRIG      		0x41
#define VSYNC_MASK         		0x01
#define SHUTTER_MASK       		0x02
#define CAP_DONE_MASK      		0x08

#define ARDUCAM_SPI_RX_BUF_SIZE 0x1000
#define ARDUCAM_SPI_TX_BUF_SIZE 0x40
#define OV2640_MAX_FIFO_SIZE	0x5FFFF

// External IO descriptors
extern struct io_descriptor *arducam_i2c_io;
extern struct io_descriptor *arducam_spi_io;

// External SPI Buffers
extern uint8_t ardu_spi_rx_buffer[ARDUCAM_SPI_RX_BUF_SIZE];
extern uint8_t ardu_spi_tx_buffer[ARDUCAM_SPI_TX_BUF_SIZE];
extern struct spi_xfer ardu_xfer;

// Function Prototypes
status_t init_arducam_hardware(void);
uint32_t arducam_i2c_write(uint8_t addr, uint8_t *data, uint16_t size);
uint32_t arducam_i2c_multi_write(const struct sensor_reg reglist[]);
uint32_t arducam_i2c_read(uint8_t addr, uint8_t *readBuf, uint16_t size);
int32_t arducam_spi_write(uint8_t addr, uint8_t data);
int8_t arducam_spi_read(uint8_t addr);
void capture(void);
void capture_rtt(void);

#endif // ARDUCAM_DRIVER_H

