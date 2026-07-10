/**
 * stxg2.h
 *
 * Driver for the CUBECOM STXG2 S-band transmitter
 * Control path/protocol: I2C 
 * Data path/protocol: SPI on the CSK header (routed via internal crossbar).
 *
 * Created for PVDXosV2
 */

#ifndef STXG2_H
#define STXG2_H

#include <stdbool.h>
#include <stdint.h>
#include "globals.h"

// Register map (User manual section 9)  
#define I2C_ADDR        0x26 
#define REG_ID          0x0001 // reads 0x53545832 = "STX2" 
#define REG_UPTIME      0x0006
#define REG_SCRATCHPAD  0x0007 
#define REG_I2C_STATUS  0x0010
#define REG_SPI_CTRL    0x002C
#define REG_SPI_STATUS  0x002D
#define REG_BOARD_STATUS 0x0030
#define REG_TEMP_0       0x0032 
#define REG_MODE        0x0060
#define REG_ENCODING    0x0062
#define REG_SYMBOL_RATE 0x0063 // 100-5000 ksps
#define REG_FREQUENCY   0x0064 // 2200000-2290000 kHz
#define REG_MEM_WR_CTRL  0x0090
#define REG_MEM_WR_INDEX 0x0091
#define REG_MEM_WR_STAT  0x0094
#define REG_MEM_RD_CTRL  0x00A0
#define REG_MEM_RD_INDEX 0x00A1
#define REG_MEM_RD_LEN   0x00A2
#define REG_MEM_RD_STAT  0x00A3
#define REG_MEM_RT_QUEUE 0x00B1
#define REG_TF_CONF     0x0112 
#define REG_CROSSBAR    0x0120
#define REG_PA_STATUS_0 0x0190
#define REG_PA_CONF_0   0x0192 

// Operating Mode enums (listed USM pg 21-22)
typedef enum {
    STXG2_MODE_STANDBY      = 0x00,
    STXG2_MODE_FLASH_ACCESS = 0x01,
    STXG2_MODE_CONFIG       = 0x02,
    STXG2_MODE_TRANSMIT     = 0x03,
} stxg2_mode_t;

// Crossbar input codes (tables 9 to 15) 
#define XBAR_IN_TPGEN 0x1
#define XBAR_IN_HS0   0x2
#define XBAR_IN_HS1   0x3
#define XBAR_IN_SPI   0x4
#define XBAR_IN_MEMRD 0x5
// Crossbar output bit offsets (register 0x0120)
#define XBAR_OUT_TPCHK_SHIFT  0
#define XBAR_OUT_FRAMER_SHIFT 4
#define XBAR_OUT_MEMWR_SHIFT  8
#define XBAR_OUT_HS0_SHIFT    12
#define XBAR_OUT_HS1_SHIFT    16

// Mem_Wr_Ctrl and Mem_Rd_Ctrl bits
#define MEM_CTRL_START (1u << 0)
#define MEM_CTRL_STOP  (1u << 1)
#define MEM_CTRL_ABORT (1u << 2)

// Driver functions 
status_t stxg2_init(void);                                 
status_t stxg2_read_reg(uint16_t addr, uint32_t *value);
status_t stxg2_write_reg(uint16_t addr, uint32_t value);
status_t stxg2_set_mode(stxg2_mode_t mode);                
status_t stxg2_configure_rf(uint32_t freq_khz, uint32_t sym_rate_ksps, uint8_t modcod, uint8_t power_dbm);
status_t stxg2_route(uint8_t input_code, uint8_t output_shift);
status_t stxg2_store_begin(uint32_t start_index);          
status_t stxg2_store_end(void);
status_t stxg2_downlink_begin(uint32_t start_index, uint32_t num_packets);
status_t stxg2_request_retransmit(uint32_t packet_id, uint8_t count);
status_t stxg2_get_board_status(uint32_t *status);

#endif