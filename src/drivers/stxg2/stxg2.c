/**
 * stxg2.c
 *
 * Driver for the CUBECOM STXG2 S-band transmitter
 * Control path/protocol: I2C
 * Data path/protocol: SPI on the CSK header (routed via crossbar)
 *
 * Created for PVDXosV2
 */

#include "stxg2.h"
#include <string.h>
#include "hal_i2c_m_sync.h"
#include "hal_spi_m_sync.h"
#include "logging.h"
#include "globals.h"

extern struct i2c_m_sync_desc I2C_STXG2; // from ASF/driver_init
extern struct spi_m_sync_descriptor SPI_SBAND;

uint8_t stxg2_spi_tx_buffer[SBAND_SPI_BUFFER_CAPACITY] = {0x00};
struct spi_xfer xfer = {.rxbuf = NULL, .txbuf = stxg2_spi_tx_buffer, .size = 0};

/**
 * STXG2 ICD write format:
 * 16-bit register address first, then 32-bit data, LSB first
 */
status_t stxg2_write_reg(uint16_t addr, uint32_t value) {
    uint8_t buf[6];
    buf[0] = (uint8_t)(addr & 0xFF);
    buf[1] = (uint8_t)((addr >> 8) & 0xFF);
    buf[2] = (uint8_t)(value & 0xFF);
    buf[3] = (uint8_t)((value >> 8) & 0xFF);
    buf[4] = (uint8_t)((value >> 16) & 0xFF);
    buf[5] = (uint8_t)((value >> 24) & 0xFF);

    struct _i2c_m_msg msg = {
        .addr = I2C_ADDR,
        .len = sizeof(buf),
        .buffer = buf,
        .flags = I2C_M_SEVEN | I2C_M_STOP, // controller infers write, sets R/W bit to low from these flags
    };
    if (_i2c_m_sync_transfer(&I2C_STXG2.device, &msg) != 0) {
        return ERROR_I2C_FAILED;
    }
    return SUCCESS;
}

status_t stxg2_read_reg(uint16_t addr, uint32_t *value) {
    uint8_t addr_buf[2] = {(uint8_t)(addr & 0xFF), (uint8_t)((addr >> 8) & 0xFF)};
    uint8_t data_buf[4] = {0};

    struct _i2c_m_msg wr = {
        .addr = I2C_ADDR,
        .len = 2,
        .buffer = addr_buf,
        .flags = I2C_M_SEVEN,
    };
    struct _i2c_m_msg rd = {
        .addr = I2C_ADDR,
        .len = 4,
        .buffer = data_buf,
        .flags = I2C_M_SEVEN | I2C_M_RD | I2C_M_STOP,
    };
    if (_i2c_m_sync_transfer(&I2C_STXG2.device, &wr) != 0) return ERROR_I2C_FAILED;
    if (_i2c_m_sync_transfer(&I2C_STXG2.device, &rd) != 0) return ERROR_I2C_FAILED;

    // value is LSB first (right to left) OR MSB first (left to right)
    *value = (uint32_t)data_buf[3] << 24 |
             (uint32_t)data_buf[2] << 16 |
             (uint32_t)data_buf[1] << 8  |
             (uint32_t)data_buf[0];

    return SUCCESS;
}

// Doing some sanity checks to make sure we're talking to the STXG2 and that read/writes work
status_t stxg2_init(void) {
    // Checking that stxg2 id register contains stxg2
    uint32_t id = 0;
    ret_err_status(stxg2_read_reg(REG_ID, &id), "stxg2: ID read failed");
    if (id != 0x53545832) { // "STX2" in ASCII
        fatal("stxg2: bad ID 0x%08lx", id);
        return ERROR_SANITY_CHECK_FAILED;
    }
    // Scratchpad write and read testing
    ret_err_status(stxg2_write_reg(REG_SCRATCHPAD, 0xB5E0CAFE), "stxg2: scratchpad write failed");
    uint32_t sp = 0;
    ret_err_status(stxg2_read_reg(REG_SCRATCHPAD, &sp), "stxg2: scratchpad read failed");
    if (sp == 0xB5E0CAFE) {
        return SUCCESS;
    }
    return ERROR_SANITY_CHECK_FAILED;
}

// see ICM pg 62
status_t stxg2_set_mode(stxg2_mode_t mode) {
    ret_err_status(stxg2_write_reg(REG_MODE, (uint32_t)mode), "stxg2: mode write failed");
    // poll until busy (bit 8) clears and mode field is same as argument
    for (int i = 0; i < 200; i++) {
        uint32_t v = 0;
        ret_err_status(stxg2_read_reg(REG_MODE, &v), "stxg2: mode poll");
        if (v & (1u << 9)) return ERROR_PROCESSING_FAILED; // bit 9 is Error bit
        if (!(v & (1u << 8)) && ((v & 0xFF) == (uint32_t)mode)) return SUCCESS;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ERROR_NOT_READY;
}

// Setting radio freq, baud rate, power, modcod, assuming default RRC=0.2, pilots on, normal fec frames
status_t stxg2_configure_rf(uint32_t freq_khz, uint32_t sym_rate_ksps, uint8_t modcod, uint8_t power_dbm) {
    ret_err_status(stxg2_write_reg(REG_FREQUENCY, freq_khz), "stxg2: freq");
    ret_err_status(stxg2_write_reg(REG_SYMBOL_RATE, sym_rate_ksps), "stxg2: symbol rate");

    // keep default RRC=0.2, pilots on, normal fec frames, set MODCOD  (ICM pg 62)
    uint32_t enc = 0;
    ret_err_status(stxg2_read_reg(REG_ENCODING, &enc), "stxg2: enc rd");
    enc = (enc & ~0xFFu) | modcod;
    ret_err_status(stxg2_write_reg(REG_ENCODING, enc), "stxg2: enc wr");
    return stxg2_write_reg(REG_PA_CONF_0, power_dbm);
}

// writes to crossbar register, which is a 32-bit register divided into 4-bit fields
// one field per output, and you write the code of whichever input you want feeding that output
// (see ICM pg 28)
status_t stxg2_route(uint8_t input_code, uint8_t output_shift) {
    uint32_t xbar = 0;
    ret_err_status(stxg2_read_reg(REG_CROSSBAR, &xbar), "stxg2: xbar rd");
    xbar &= ~(0xFu << output_shift); // clears 4 bits at position output_shift
    xbar |= ((uint32_t)input_code & 0xF) << output_shift; // insert low 4 bits of input_code into slot
    return stxg2_write_reg(REG_CROSSBAR, xbar);
}

// for transmitting photo over to stxg2 flash
// start_index - memory index to begin reading from
// after this returns SUCCESS, stream the image bytes over SPI, then call stxg2_store_end() - still TODO
// might add an optional argument to specify number of bytes to store
status_t stxg2_store_begin(uint32_t start_index) {
    // set flash access mode
    ret_err_status(stxg2_set_mode(STXG2_MODE_FLASH_ACCESS), "stxg2: mode");
    // route the crossbar: spi to flash memory write
    ret_err_status(stxg2_route(XBAR_IN_SPI, XBAR_OUT_MEMWR_SHIFT), "stxg2: route spi to memwr");
    // enable spi
    ret_err_status(stxg2_write_reg(REG_SPI_CTRL, 0x1), "stxg2: spi en");
    // set the start index for the memwrite
    ret_err_status(stxg2_write_reg(REG_MEM_WR_INDEX, start_index), "stxg2: wr index");
    return stxg2_write_reg(REG_MEM_WR_CTRL, MEM_CTRL_START);
}

status_t stxg2_store(uint8_t *img_buf, size_t img_sz) {
    size_t offset = 0;
    size_t chunk_sz = 0;
    size_t total = img_sz;
    uint32_t spi_status = 0;

    while(img_sz > 0) {
        // Check conditions:
        // 1. Is buffer full?
        // 2. Is buffer overflow?
        // 3. Is buffer ready?
        ret_err_status(stxg2_read_reg(REG_SPI_STATUS, &spi_status), "stxg2: spi status rd");
        // Overflow should be handled outside here as a fatal condition on the
        // SPI and should probably be reset in some way??
        if (spi_status & SPI_STATUS_B0v) {
            fatal("stxg2:SPI in overflow state, data lost");
            return ERROR_SPI_TRANSFER_FAILED;
        }
        // Not ready and full are both treated as retry conditions to let
        // internal SPI xbar buffer drain
        if(spi_status & SPI_STATUS_BF1) {
            debug("Attempt to write to stxg2:SPI when SPI buffer full");
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        } else if (!(spi_status & SPI_STATUS_BRy)) {
            debug("Attempt to write to stxg2:SPI when SPI not ready");
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        // Room left in the device SPI buffer before it overflows
        size_t room = SBAND_SPI_BUFFER_CAPACITY - SPI_STATUS_COUNT(spi_status);
        if (room == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        chunk_sz = (img_sz < room) ? img_sz : room;

        // Set up buffer
        xfer.size = chunk_sz;
        memcpy(stxg2_spi_tx_buffer, img_buf + offset, chunk_sz);

        // Transfer and hope you don't die
        SBAND_CS_LOW();
        int32_t response = spi_m_sync_transfer(&SPI_SBAND, &xfer);
        SBAND_CS_HIGH();

        if (response != (int32_t)chunk_sz) {
            return ERROR_SPI_TRANSFER_FAILED;
        }

        offset += chunk_sz;
        img_sz -= chunk_sz;
    }
    uint32_t rx_words = 0;
    ret_err_status(stxg2_read_reg(REG_SPI_RX_CNT, &rx_words), "stxg2: spi rx cnt rd");
    if (rx_words < (total + 3) / 4) {
        debug("Failed to transfer all bytes of image to STXG2");
        return ERROR_SPI_TRANSFER_FAILED;
    }
    return SUCCESS;
}

// stop pads final packet and sets the EOF flag in the Packet id
status_t stxg2_store_end(void) {
    return stxg2_write_reg(REG_MEM_WR_CTRL, MEM_CTRL_STOP);
    // check mem_wr_status to make sure no errors in write process
    uint32_t *rd_buf;
    ret_err_status(stxg2_read_reg(REG_MEM_WR_STAT, rd_buf), "stxg2: rd mem write status");
    if (!(*rd_buf & (uint32_t)(1 << 1))) { // if bit 1 is 0, meaning write not complete (usm 71)
        return ERROR_SPI_TRANSFER_FAILED;
    }
}

// assumes stxg2_configure_rf() already called
// start_index - memory index to begin reading from, num_packets - how many consecutive packets to read
status_t stxg2_downlink_begin(uint32_t start_index, uint32_t num_packets) {
    // route flash mem output into packet framing
    ret_err_status(stxg2_route(XBAR_IN_MEMRD, XBAR_OUT_FRAMER_SHIFT), "stxg2: route memrd->framer");
    // set the mode to transmit (this function polls until transmit mode is set)
    ret_err_status(stxg2_set_mode(STXG2_MODE_TRANSMIT), "stxg2: tx mode");
    // writes to memory index with the packet number (start_index) to start reading from
    ret_err_status(stxg2_write_reg(REG_MEM_RD_INDEX, start_index), "stxg2: rd index");
    // writes to mem len with num_packets, to set how many packets to read before stopping read
    ret_err_status(stxg2_write_reg(REG_MEM_RD_LEN, num_packets), "stxg2: rd len");
    // start reading, automatically moves packets from flash, wraps in CCSDS headers, modulation, radio signal
    return stxg2_write_reg(REG_MEM_RD_CTRL, MEM_CTRL_START);
}

// injects the request into retransmission queue, should execute automatically from there
// request structure: count in bits 31-24, packet_id in bits 23-0 (pg 75 ICM)
status_t stxg2_request_retransmit(uint32_t packet_id, uint8_t count) {
    uint32_t req = (packet_id & 0x00FFFFFF) | ((uint32_t)count << 24);
    return stxg2_write_reg(REG_MEM_RT_QUEUE, req);
}

status_t stxg2_get_board_status(uint32_t *status) {
    return stxg2_read_reg(REG_BOARD_STATUS, status);
}

// TODO: transfer of image bytes over SPI, checking for buffer overflow on spi to mem writes
// flushing mem before read, donwlink end function
// checking for rf_status
// error reporting make sure its consistent with either globals.h or at86 error reporting mech