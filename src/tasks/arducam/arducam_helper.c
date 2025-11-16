#include <stdint.h>
#include "arducam.h"

struct spi_xfer ardu_xfer = {.rxbuf = ardu_spi_rx_buffer, .txbuf = ardu_spi_tx_buffer, .size = 0};

int32_t ARDUCAMSPIWrite(uint8_t addr, uint8_t data) {
    gpio_set_pin_level(Camera_CS, 0);

    uint8_t value[2] = {addr | 0x80, data};  // Set MSB high for write operation
    int32_t response = io_write(arducam_spi_io, value, 2);

    gpio_set_pin_level(Camera_CS, 1);
    return response;
}

int8_t ARDUCAMSPIRead(uint8_t addr) {
    gpio_set_pin_level(Camera_CS, 0);

    uint8_t tx_data = addr & 0x7F;  // Set MSB low for read operation
    uint8_t rx_data = 0;

    io_write(arducam_spi_io, &tx_data, 1);
    io_read(arducam_spi_io, &rx_data, 1);

    gpio_set_pin_level(Camera_CS, 1);
    return rx_data;
}

uint8_t get_bit(uint8_t addr, uint8_t bit)
{
  uint8_t temp;
  temp = ARDUCAMSPIRead(addr);
  temp = temp & bit;
  return temp;
}

uint32_t read_fifo_length(void)
{
    uint32_t len1,len2,len3,length=0;
    len1 = ARDUCAMSPIRead(FIFO_SIZE1);
    len2 = ARDUCAMSPIRead(FIFO_SIZE2);
    len3 = ARDUCAMSPIRead(FIFO_SIZE3) & 0x7f;
    length = ((len3 << 16) | (len2 << 8) | len1) & 0x07fffff;
    return length;	
}

// void capture(void) {
//     ARDUCAMSPIWrite(ARDUCHIP_FIFO, FIFO_CLEAR_MASK); // Flush the FIFO
//     ARDUCAMSPIWrite(ARDUCHIP_FIFO, FIFO_CLEAR_MASK); // Clear the capture done flag (uses same clear mask)
//     ARDUCAMSPIWrite(ARDUCHIP_FIFO, FIFO_START_MASK); // Start capture
//     while (!get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK));  // Wait for capture to finish

//     size_t len = read_fifo_length();
//     if (len >= 0x07ffff){
//         info("Oversized!");
//         return;
//     } else if (len == 0 ){
//         info("Size is 0!");
//         return;
//     }

//     gpio_set_pin_level(Camera_CS, 0);
//     ARDUCAMSPIWrite(SPI_ARDUCAM, BURST_FIFO_READ);

//     uint8_t *ffbyte = { 0xFF };
//     io_write(arducam_spi_io, ffbyte, 1);

//     size_t bufferSize = 4096;
//     ardu_xfer.size = bufferSize;

//     while (len) {
//         size_t will_copy = (len < bufferSize) ? len : bufferSize;
//         ardu_xfer.size = will_copy;
//         spi_m_sync_transfer(arducam_spi_io, &ardu_xfer);
//         len -= will_copy;
//     }

//     gpio_set_pin_level(Camera_CS, 1);
// }

void capture_rtt(void) {
    watchdog_checkin(ARDUCAM_TASK);
    ARDUCAMSPIWrite(ARDUCHIP_FIFO, FIFO_CLEAR_MASK); // Flush the FIFO
    ARDUCAMSPIWrite(ARDUCHIP_FIFO, FIFO_CLEAR_MASK); // Clear the capture done flag (uses same clear mask)
    ARDUCAMSPIWrite(ARDUCHIP_FIFO, FIFO_START_MASK); // Start capture
    while (!get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK));   // Wait for capture to finish

    // Read how many bytes were captured
    size_t len = read_fifo_length();
    if (len >= OV2640_MAX_FIFO_SIZE) {
        info("Oversized!");
        return;
    } else if (len == 0 ){
        info("Size is 0!");
        return;
    }

    // Make channel 2 block so we don't lose image bytes if host is slow
    SEGGER_RTT_SetFlagsUpBuffer(2, SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);
    info("IMG_BEGIN len=%u", (unsigned)len);

    // Start burst read
    gpio_set_pin_level(Camera_CS, 0);
    uint8_t cmd = BURST_FIFO_READ;
    io_write(arducam_spi_io, &cmd, 1);

    const size_t bufferSize = ARDUCAM_SPI_RX_BUF_SIZE;
    ardu_xfer.txbuf = NULL;  // HAL will send dummy byte automatically
    ardu_xfer.rxbuf = ardu_spi_rx_buffer;

    while (len) {
        size_t will_copy = (len < bufferSize) ? len : bufferSize;
        ardu_xfer.size = will_copy;
        spi_m_sync_transfer(&SPI_0, &ardu_xfer);
        SEGGER_RTT_Write(2, ardu_spi_rx_buffer, (unsigned)will_copy);
        len -= will_copy;
        watchdog_checkin(ARDUCAM_TASK);
    }

    gpio_set_pin_level(Camera_CS, 1);
    info("IMG_END");
}
