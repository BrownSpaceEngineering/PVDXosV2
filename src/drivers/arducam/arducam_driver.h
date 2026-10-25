#ifndef ARDUCAM_DRIVER_H
#define ARDUCAM_DRIVER_H

#include <atmel_start.h>
#include <driver_init.h>
#include <stdbool.h>
#include <stdint.h>

#include "globals.h"
#include "rtos_start.h"

struct sensor_reg {
    uint8_t reg;
    uint8_t val;
};

// Hardware Defines
#define ARDUCAM_ADDR 0x60
#define SPI_ARDUCAM 0x00

// Arducam Defines
#define OV2640_CHIPID_HIGH 0x0A
#define OV2640_CHIPID_LOW 0x0B

#define ARDUCHIP_FIFO 0x04
#define FIFO_CLEAR_MASK 0x01
#define FIFO_START_MASK 0x02

#define FIFO_SIZE1 0x42 // Camera write FIFO size[7:0] for burst to read
#define FIFO_SIZE2 0x43 // Camera write FIFO size[15:8]
#define FIFO_SIZE3 0x44 // Camera write FIFO size[18:16]

#define BURST_FIFO_READ 0x3C  // Burst FIFO read operation
#define SINGLE_FIFO_READ 0x3D // Single FIFO read operation

#define ARDUCHIP_TEST1 0x00 // TEST REGISTER

#define ARDUCHIP_TRIG 0x41
#define VSYNC_MASK 0x01
#define SHUTTER_MASK 0x02
#define CAP_DONE_MASK 0x08

#define ARDUCAM_SPI_RX_BUF_SIZE 0x1000
#define ARDUCAM_SPI_TX_BUF_SIZE 0x40
#define OV2640_MAX_FIFO_SIZE 0x5FFFF
#define ARDUCAM_CAPTURE_TIMEOUT_MS 2000 // well under the task's watchdog timeout
#define ARDUCAM_RTT_TIMEOUT_MS 1000     // Maximum wait for the host to accept each complete packet

// SEGGER RTT up-channel used to stream captured images (configured by the camera task).
#define CAMERA_RTT_OUTPUT_CHANNEL 2
#define SEGGER_RTT_IMAGE_BUFFER_SIZE 4096
#define ARDUCAM_RTT_HEADER_SIZE 28U
#define ARDUCAM_RTT_MAX_PAYLOAD (SEGGER_RTT_IMAGE_BUFFER_SIZE - 1U - ARDUCAM_RTT_HEADER_SIZE)

// External IO descriptors
extern struct io_descriptor *arducam_i2c_io;
extern struct io_descriptor *arducam_spi_io;

// External SPI Buffers
extern uint8_t ardu_spi_rx_buffer[ARDUCAM_SPI_RX_BUF_SIZE];
extern uint8_t ardu_spi_tx_buffer[ARDUCAM_SPI_TX_BUF_SIZE];
extern struct spi_xfer ardu_xfer;

// Function Prototypes
status_t init_arducam_hardware(void);
int32_t arducam_i2c_write(uint8_t addr, uint8_t *data, uint16_t size);
int32_t arducam_i2c_multi_write(const struct sensor_reg reglist[]);
int32_t arducam_i2c_read(uint8_t addr, uint8_t *readBuf, uint16_t size);
int32_t arducam_spi_write(uint8_t addr, uint8_t data);
status_t arducam_spi_read(uint8_t addr, uint8_t *data);
status_t capture(void);
typedef bool (*arducam_cancel_fn)(void);
// The caller owns channel 2 configuration: 4096 bytes, NO_BLOCK_SKIP, never reset while the host reads.
status_t capture_rtt_cancellable(arducam_cancel_fn cancelled);
status_t capture_rtt(void);

#endif // ARDUCAM_DRIVER_H
