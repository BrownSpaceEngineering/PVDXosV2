/**
 * stxg2.h
 *
 * CUBECOM STXG2 (VestaLink S25) S-band transmitter driver.
 * Control: I2C. Data: receive-only SPI on the CSK header, crossbar input 0x4.
 * The radio framer is off. PVDX frames its own data.
 * Refs: ICD-01-10022-A6 (ICD), USM-01-10022-A8 (USM).
 */

#ifndef STXG2_H
#define STXG2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atmel_start.h"
#include "globals.h"

#define STXG2_I2C_ADDR 0x26
#define STXG2_ID_VALUE 0x53545832u // "STX2" (USM 9.2.1.1)

// Pins from the PVDX-SAMD-PinConfig README.
// WARNING: main's ASF labels PB19 as BURN. Fix upstream.
#ifndef SBAND_CS
    #define SBAND_CS GPIO(GPIO_PORTB, 19) // Active low
#endif
#ifndef SBAND_EN
    #define SBAND_EN GPIO(GPIO_PORTC, 23) // High = on (ICD 2.2)
#endif
#ifndef SBAND_RST
    #define SBAND_RST GPIO(GPIO_PORTB, 20) // Low = reset (ICD 2.2)
#endif

#define SBAND_CS_LOW() gpio_set_pin_level(SBAND_CS, false)
#define SBAND_CS_HIGH() gpio_set_pin_level(SBAND_CS, true)

/* ---------- Registers (USM 9.1) ---------- */

// General
#define REG_ID 0x0001
#define REG_SERIAL 0x0002
#define REG_SW_VERSION 0x0003
#define REG_FW_VERSION 0x0004
#define REG_HW_VERSION 0x0005
#define REG_UPTIME 0x0006 // ms
#define REG_SCRATCHPAD 0x0007

// Communication. Status registers are clear-on-read.
#define REG_I2C_STATUS 0x0010
#define REG_I2C_CONFIG 0x0011
#define REG_CAN_STATUS 0x0012
#define REG_CAN_CONFIG 0x0013
#define REG_UART_STATUS 0x0014
#define REG_UART_CONFIG 0x0015
#define REG_COMM_UNLOCK 0x001F

// Data source. HS nodes (0x0020-0x002B) unused.
#define REG_SPI_CTRL 0x002C   // USM 9.2.3.7
#define REG_SPI_STATUS 0x002D // USM 9.2.3.8
#define REG_SPI_RX_CNT 0x002E // Words received (USM 9.2.3.9)

// Telemetry
#define REG_BOARD_STATUS 0x0030
#define REG_RF_STATUS 0x0031
#define REG_TEMP_0 0x0032 // PA, PA SMPS
#define REG_TEMP_1 0x0033 // Board, FPGA
#define REG_TEMP_2 0x0034 // Board x2
#define REG_TEMP_3 0x0035 // Board
#define REG_CURRENT_ERR 0x003E
#define REG_VOLTAGE_ERR 0x003F
#define REG_CURRENTS 0x0040 // To 0x004F
#define REG_VOLTAGES 0x0050 // To 0x005F

// Control
#define REG_MODE 0x0060
#define REG_SYMBOL_RATE 0x0061 // ksps
#define REG_FREQUENCY 0x0062   // kHz
#define REG_ENCODING_CTRL 0x0065
#define REG_DVBS2_CONF 0x0066
#define REG_CCSDS_CONF 0x0067
#define REG_ENCODING_STATUS 0x0068 // DVB-S2 dummy frame count

// Thermal protection
#define REG_PROTECTION_LEVEL_1 0x0070
#define REG_PROTECTION_LEVEL_2 0x0071
#define REG_PROTECTION_LEVEL_3 0x0072
#define REG_BOARD_TLIM 0x0073

// Memory. Not writable in Standby.
#define REG_MEMORY_STATUS 0x0080
#define REG_MEMORY_SIZE 0x0081 // Packets
#define REG_MEMORY_HLTH_0 0x0083
#define REG_MEMORY_HLTH_1 0x0084
#define REG_MEM_WR_CTRL 0x0090
#define REG_MEM_WR_INDEX 0x0091 // Packet index
#define REG_MEM_WR_LEN_0 0x0092 // Bytes, low word. 0 = until stop.
#define REG_MEM_WR_LEN_1 0x0093 // Bytes, high word
#define REG_MEM_WR_STAT 0x0094
#define REG_MEM_WR_CNT_0 0x0095 // Bytes, low word
#define REG_MEM_WR_CNT_1 0x0096 // Bytes, high word
#define REG_MEM_WR_PKT_CNT 0x0097
#define REG_MEM_WR_ERR 0x0098
#define REG_MEM_RD_CTRL 0x00A0
#define REG_MEM_RD_INDEX 0x00A1 // Packet index
#define REG_MEM_RD_LEN 0x00A2   // Packets. 0 = until stop or EOF.
#define REG_MEM_RD_STAT 0x00A3
#define REG_MEM_RD_CNT 0x00A4
#define REG_MEM_RD_ERR 0x00A5
#define REG_MEM_RT_CTRL 0x00B0
#define REG_MEM_RT_QUEUE 0x00B1
#define REG_MEM_RT_STAT 0x00B2
#define REG_MEM_RT_CNT 0x00B4
#define REG_MEM_RT_ERR 0x00B5

// Data routing
#define REG_TF_CTRL 0x0110
#define REG_TF_BUF_CNT 0x0111
#define REG_TF_CONF 0x0112
#define REG_TF_DATA_CNT 0x0113
#define REG_TF_OID_CNT 0x0114
#define REG_TF_STREAM_CNT 0x0115
#define REG_CROSSBAR 0x0120
#define REG_TP_GEN_CTRL 0x0121
#define REG_TP_GEN_CNT 0x0122
#define REG_TP_GEN_RATE 0x0123
#define REG_TP_CHK_CTRL 0x0124
#define REG_TP_CHK_STAT 0x0125
#define REG_TP_CHK_SYNC_CNT 0x0126
#define REG_TP_CHK_UNSYNC_CNT 0x0127
#define REG_TP_CHK_RATE 0x0128
#define REG_TP_CHK_LST_CNT 0x0129

// PA
#define REG_PA_STATUS_0 0x0190
#define REG_PA_STATUS_1 0x0191
#define REG_PA_CONF_0 0x0192 // Target power, dBm
#define REG_PA_CONF_1 0x0193
#define REG_PA_LIM 0x0197
#define REG_PA_TIMEOUT 0x0198

/* ---------- Fields ---------- */

// Modes (USM 9.2.5.1, table 9-10)
typedef enum {
    STXG2_MODE_STANDBY = 0x00,
    STXG2_MODE_FLASH_ACCESS = 0x01,
    STXG2_MODE_CONFIG = 0x02,
    STXG2_MODE_TRANSMIT = 0x03,
} stxg2_mode_t;

// MODE (USM 9.2.5.1)
#define MODE_MODE_MSK 0xFFu
#define MODE_BUSY (1u << 8)
#define MODE_ERROR (1u << 9)

// I2C_STAT (USM 9.2.2.1)
#define I2C_STATUS_INVALID_ADDR (1u << 0)
#define I2C_STATUS_OUT_OF_RANGE (1u << 1)
#define I2C_STATUS_NOT_WRITEABLE (1u << 2)
#define I2C_STATUS_INTERNAL_ERR (1u << 3)
#define I2C_STATUS_BUF_OVERFLOW (1u << 4)
#define I2C_STATUS_MSK 0x1Fu

// SPI_CTRL (USM 9.2.3.7). Mode applies on enable.
#define SPI_CTRL_ENABLE (1u << 0)
#define SPI_CTRL_MODE_POS 1u
#define SPI_CTRL_MODE(m) (((uint32_t)(m) & 0x3u) << SPI_CTRL_MODE_POS)
#define SPI_CTRL_READY_POLARITY (1u << 3)
#define STXG2_SPI_MODE 0u // CPOL 0, CPHA 0

// SPI_STAT (USM 9.2.3.8). Ready = below half full. Count = words in buffer.
#define SPI_STATUS_FULL (1u << 0)
#define SPI_STATUS_OVERFLOW (1u << 1)
#define SPI_STATUS_READY (1u << 2)
#define SPI_STATUS_RX_RECVD (1u << 3)
#define SPI_STATUS_COUNT_POS 16u
#define SPI_STATUS_COUNT_MSK (0xFFFFu << SPI_STATUS_COUNT_POS)
#define SPI_STATUS_COUNT(reg) (((reg) & SPI_STATUS_COUNT_MSK) >> SPI_STATUS_COUNT_POS)

#define STXG2_SPI_BUF_WORDS 1024u      // Radio SPI buffer
#define STXG2_SPI_CHUNK_BYTES 2048u    // Half the radio buffer
#define STXG2_PACKET_DATA_BYTES 16380u // Payload per memory packet (USM 9.2.7.5)

// RF_STAT (USM 9.2.4.2)
#define RF_STATUS_RF_SYNTH_LOCK (1u << 0)
#define RF_STATUS_IF_SYNTH_LOCK (1u << 1)
#define RF_STATUS_LOCKED (RF_STATUS_RF_SYNTH_LOCK | RF_STATUS_IF_SYNTH_LOCK)

// MEMORY_STAT (USM 9.2.7.1, table 9-18)
#define MEMORY_STATUS_STATE(reg) ((reg) & 0xFFu)
#define MEMORY_STATE_OFF 0u
#define MEMORY_STATE_IDLE 1u
#define MEMORY_STATE_WRITE_BUSY 2u
#define MEMORY_STATE_READ_BUSY 3u
#define MEMORY_STATE_RT_BUSY 4u

// MEM_WR_CTRL, MEM_RD_CTRL (USM 9.2.7.4, 9.2.7.11)
#define MEM_CTRL_START (1u << 0)
#define MEM_CTRL_STOP (1u << 1)
#define MEM_CTRL_ABORT (1u << 2)
#define MEM_RD_CTRL_EOF_STOP (1u << 3)

// MEM_WR_STAT, MEM_RD_STAT (USM 9.2.7.7, 9.2.7.14)
#define MEM_STAT_ACTIVE (1u << 0)
#define MEM_STAT_COMPLETE (1u << 1)
#define MEM_STAT_ERROR (1u << 2)
#define MEM_STAT_OOB (1u << 3)
#define MEM_STAT_INVALID_INDEX (1u << 4)

// MEM_RT_CTRL (USM 9.2.7.17)
#define MEM_RT_CTRL_START (1u << 0)
#define MEM_RT_CTRL_STOP (1u << 1)
#define MEM_RT_CTRL_PRIORITY (1u << 2)
#define MEM_RT_CTRL_CLEAR_QUEUE (1u << 3)

// MEM_RT_QUEUE (USM 9.2.7.18)
#define MEM_RT_QUEUE_INDEX_MSK 0x00FFFFFFu
#define MEM_RT_QUEUE_LEN_POS 24u

// DVBS2_CONF (USM 9.2.5.5)
#define DVBS2_CONF_MODCOD_MSK 0x1Fu
#define DVBS2_MODCOD_MAX 28u

// TF_CTRL (USM 9.2.8.1). The radio sets Enable on entering Config. The driver clears it.
#define TF_CTRL_ENABLE (1u << 0)
#define TF_CTRL_BUF_FLUSH (1u << 4) // Unused. Framer only.

// Crossbar inputs (USM 9.2.8.7, table 9-22). 0 disconnects.
#define XBAR_IN_NONE 0x0
#define XBAR_IN_TPGEN 0x1
#define XBAR_IN_HS0 0x2
#define XBAR_IN_HS1 0x3
#define XBAR_IN_SPI 0x4
#define XBAR_IN_MEMRD 0x5
// Crossbar output shifts
#define XBAR_OUT_TPCHK_SHIFT 0
#define XBAR_OUT_FRAMER_SHIFT 4
#define XBAR_OUT_MEMWR_SHIFT 8
#define XBAR_OUT_HS0_SHIFT 12
#define XBAR_OUT_HS1_SHIFT 16

// RF limits (USM 9.1, 9.2.5.2, 9.2.5.3, 9.2.9.2)
#define STXG2_FREQ_MIN_KHZ 2150000u
#define STXG2_FREQ_MAX_KHZ 2450000u // Standard unit stops at 2290000. Raised for the amateur band (2400-2450 MHz) unit.
#define STXG2_SYM_RATE_MIN_KSPS 100u
#define STXG2_SYM_RATE_MAX_KSPS 5000u
#define STXG2_SYM_RATE_STEP_KSPS 100u
#define STXG2_POWER_MIN_DBM 14u // Flatsat 14-20
#define STXG2_POWER_MAX_DBM 33u // Flight 27-33

/* ---------- Functions ---------- */

// Setup, registers
status_t stxg2_init(void);
status_t stxg2_read_reg(uint16_t addr, uint32_t *value);
status_t stxg2_write_reg(uint16_t addr, uint32_t value);
status_t stxg2_check_i2c_status(void);

// Control
status_t stxg2_set_mode(stxg2_mode_t mode);
status_t stxg2_configure_rf(uint32_t freq_khz, uint32_t sym_rate_ksps, uint8_t modcod, uint8_t power_dbm);
status_t stxg2_route(uint8_t input_code, uint8_t output_shift);

// Store: begin, store (repeat), end
status_t stxg2_store_begin(uint32_t start_index);
status_t stxg2_store(const uint8_t *data_buf, size_t sz);
status_t stxg2_store_end(uint32_t *packets_written);

// Downlink
status_t stxg2_downlink_begin(uint32_t start_index, uint32_t num_packets);
status_t stxg2_downlink_end(void);
status_t stxg2_request_retransmit(uint32_t packet_id, uint8_t count);

// Telemetry
status_t stxg2_get_board_status(uint32_t *status);
status_t stxg2_get_rf_status(uint32_t *status);

#endif
