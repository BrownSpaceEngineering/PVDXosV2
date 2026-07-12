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
#include "atmel_start.h"
#include "globals.h"

#define I2C_ADDR         0x26

#define SBAND_CS_LOW() gpio_set_pin_level(SBAND_CS, 0);
#define SBAND_CS_HIGH() gpio_set_pin_level(SBAND_CS, 1);

// General registers
#define REG_ID           0x0001  // Identifier; reads 0x53545832 = "STX2"
#define REG_SERIAL       0x0002  // Board serial number
#define REG_SW_VERSION   0x0003  // Software version (Major, Minor, Patch)
#define REG_FW_VERSION   0x0004  // Firmware version (Major, Minor, Patch)
#define REG_HW_VERSION   0x0005  // Hardware version (Major, Minor, Patch)
#define REG_UPTIME       0x0006  // Increments continuously; comms check
#define REG_SCRATCHPAD   0x0007  // Test register for comms read/write debugging

// Communication registers
#define REG_I2C_STATUS   0x0010  // I2C status (Busy/Invalid/OoR/Overflow), clear-on-read
#define REG_I2C_CONFIG   0x0011  // I2C link configuration
#define REG_CAN_STATUS   0x0012  // CAN status (Busy/Invalid/OoR/Overflow), clear-on-read
#define REG_CAN_CONFIG   0x0013  // CAN link configuration
#define REG_UART_STATUS  0x0014  // UART status (Busy/Invalid/OoR/Overflow), clear-on-read
#define REG_UART_CONFIG  0x0015  // UART link configuration
#define REG_COMM_UNLOCK  0x001F  // Comms link configuration access control

// Data source registers
// HS_* high-speed data interface (SpaceWire/LVDS) 0x0020-0x002B omitted; unused.
#define REG_SPI_CTRL     0x002C  // SPI link and usage status
#define REG_SPI_STATUS   0x002D  // SPI interface control
#define REG_SPI_RX_CNT   0x002E  // Count of 32-bit words received/passed via SPI

// Telemetry registers
#define REG_BOARD_STATUS 0x0030  // General device status
#define REG_RF_STATUS    0x0031  // IF Synth Lock, RF Synth Lock
#define REG_TEMP_0       0x0032  // PA and PA SMPS temperature (2x 16-bit)
#define REG_TEMP_1       0x0033  // Board and FPGA temperature (2x 16-bit)
#define REG_TEMP_2       0x0034  // Board temperature (2x 16-bit)
#define REG_TEMP_3       0x0035  // Board temperature (1x 16-bit)
#define REG_CURRENT_ERR  0x003E  // Current sensor out-of-range flags
#define REG_VOLTAGE_ERR  0x003F  // Voltage sensor out-of-range flags
#define REG_CURRENTS     0x0040  // 0x0040-0x004F: monitored current values
#define REG_VOLTAGES     0x0050  // 0x0050-0x005F: monitored voltage values

// Control registers
#define REG_MODE            0x0060  // Transmitter state: Standby/Flash/Config/Transmit
#define REG_ENCODING        0x0062  // DVB-S2 encoding settings
#define REG_SYMBOL_RATE     0x0063  // Symbol rate: 100-5000 ksps, 100 ksps steps
#define REG_FREQUENCY       0x0064  // Frequency: 2200000-2290000 kHz, 1 MHz steps
#define REG_ENCODING_STATUS 0x0066  // Dummy-frame count when no data available

// Thermal protection registers
#define REG_PROTECTION_LEVEL_1 0x0070  // PA over-temperature protection level 1
#define REG_PROTECTION_LEVEL_2 0x0071  // PA over-temperature protection level 2
#define REG_PROTECTION_LEVEL_3 0x0072  // PA over-temperature protection level 3
#define REG_BOARD_TLIM         0x0073  // Board over-temperature protection

// Memory registers
#define REG_MEMORY_STATUS   0x0080  // On-board flash status (state/usage)
#define REG_MEMORY_SIZE     0x0081  // Size of memory available to write to
#define REG_MEMORY_HLTH_0   0x0083  // Memory usage information
#define REG_MEMORY_HLTH_1   0x0084  // Memory usage information
#define REG_MEM_WR_CTRL     0x0090  // Memory write interface control
#define REG_MEM_WR_INDEX    0x0091  // Write start address (increments in 16KB clusters)
#define REG_MEM_WR_LEN_0    0x0092  // Bytes to write (lower 4 bytes)
#define REG_MEM_WR_LEN_1    0x0093  // Bytes to write (upper 4 bytes)
#define REG_MEM_WR_STAT     0x0094  // Write interface status
#define REG_MEM_WR_CNT_0    0x0095  // Bytes written (lower 4 bytes)
#define REG_MEM_WR_CNT_1    0x0096  // Bytes written (upper 4 bytes)
#define REG_MEM_WR_PKT_CNT  0x0097  // Packets written into memory
#define REG_MEM_WR_ERR      0x0098  // Write error information field
#define REG_MEM_RD_CTRL     0x00A0  // Memory read interface control
#define REG_MEM_RD_INDEX    0x00A1  // Read start address (increments in 16KB clusters)
#define REG_MEM_RD_LEN      0x00A2  // Bytes to read from memory
#define REG_MEM_RD_STAT     0x00A3  // Read interface status (see note in accompanying message)
#define REG_MEM_RD_CNT      0x00A4  // Bytes read from memory
#define REG_MEM_RD_ERR      0x00A5  // Read error information field
#define REG_MEM_RT_CTRL     0x00B0  // Retransmit interface control
#define REG_MEM_RT_QUEUE    0x00B1  // Retransmit queue (index and length)
#define REG_MEM_RT_STAT     0x00B2  // Retransmit interface status
#define REG_MEM_RT_CNT      0x00B4  // Retransmit packets read this session
#define REG_MEM_RT_ERR      0x00B5  // Retransmit error information field

// Data routing registers
#define REG_TF_CTRL           0x0110  // Transfer frame control field
#define REG_TF_BUF_CNT        0x0111  // Bytes in transfer frame buffer
#define REG_TF_CONF           0x0112  // SCID, VCID, RT_VCID
#define REG_TF_DATA_CNT       0x0113  // Transfer frame data count
#define REG_TF_OID_CNT        0x0114  // Transfer frame idle count
#define REG_TF_STREAM_CNT     0x0115  // Transfer frame stream count
#define REG_CROSSBAR          0x0120  // Data flow/routing control
#define REG_TP_GEN_CTRL       0x0121  // Test pattern generator control
#define REG_TP_GEN_CNT        0x0122  // Bytes generated by TP generator
#define REG_TP_GEN_RATE       0x0123  // Generated test pattern data rate
#define REG_TP_CHK_CTRL       0x0124  // Test pattern checker control
#define REG_TP_CHK_STAT       0x0125  // Pattern checker status
#define REG_TP_CHK_SYNC_CNT   0x0126  // Bytes received while synced to pattern
#define REG_TP_CHK_UNSYNC_CNT 0x0127  // Bytes received while not synced
#define REG_TP_CHK_RATE       0x0128  // Received data rate
#define REG_TP_CHK_LST_CNT    0x0129  // Bytes received at EOF signal reception

// PA status and control registers
#define REG_PA_STATUS_0  0x0190  // Overtemp flags, temperature, measured RF power
#define REG_PA_STATUS_1  0x0191  // Measured current, measured RF power quanta
#define REG_PA_CONF_0    0x0192  // Target RF power for this PA
#define REG_PA_CONF_1    0x0193  // PID mode control
#define REG_PA_LIM       0x0197  // PA protection limits
#define REG_PA_TIMEOUT   0x0198  // PA timeout compare and current values

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

// SPI Impl defns
#define SBAND_SPI_BUFFER_CAPACITY 4096 // bytes


/*
    32                      16 15            4      3    2     1
    +--------------------------------------------------------------+
    |   Buffer_Count[15:0]  |   Reserved  | RxR | BRy | B0v | BF1 |
    + ------------------------------------------------------------+

    BF1: Buffer Full                 1 = overflow
    B0v: Buffer overflow             1 = data lost, err
    BRy Buffer Ready                 1 = ready, 0 = not ready
    RxR                              data received
*/

// Masks and bitfields
#define SPI_STATUS_BF1_POS 0u
#define SPI_STATUS_B0v_POS 1u
#define SPI_STATUS_BRy_POS 2u
#define SPI_STATUS_RxR_POS 3u

#define SPI_STATUS_BF1 (1u << SPI_STATUS_BF1_POS)
#define SPI_STATUS_B0v (1u << SPI_STATUS_B0v_POS)
#define SPI_STATUS_BRy (1u << SPI_STATUS_BRy_POS)
#define SPI_STATUS_RxR (1u << SPI_STATUS_RxR_POS)

#define SPI_STATUS_BUFCOUNT_POS 16u
#define SPI_STATUS_COUNT_MSK (0xFFFFu << SPI_STATUS_BUFCOUNT_POS)
#define SPI_STATUS_COUNT(reg)  (((reg) & SPI_STATUS_COUNT_MSK) >> SPI_STATUS_BUFCOUNT_POS)

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