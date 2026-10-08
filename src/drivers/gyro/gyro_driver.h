/**
 * gyro_driver.h
 *
 * Driver for the Murata SCH16T-K01 gyroscope / accelerometer (SCH1 family), reached over
 * SPI on SERCOM4 (SPI_MAGNETOMETER_GYRO).
 *
 * The register map, SPI request frames and frame-field layout in this file are taken from
 * Murata's SCH1 reference library. See the BSD license notice at the top of gyro_driver.c.
 *
 * Created: September 27, 2026
 * Ported from Murata's SCH1 reference library and the PVDX SCH16T Arduino prototype.
 */

#ifndef GYRO_DRIVER_H
#define GYRO_DRIVER_H

#include <stddef.h>

#include "atmel_start.h"
#include "globals.h"
#include "logging.h"

/* ---------- SENSOR CONFIGURATION ---------- */
// These are the settings the SCH16T Arduino prototype was validated against.

#define SCH16T_FILTER_RATE12 30 // Hz, LPF1 cut-off (-3dB) for Rate_XYZ1/2. Valid: 13, 30, 68, 235, 280, 370
#define SCH16T_FILTER_ACC12 30  // Hz, LPF1 cut-off (-3dB) for Acc_XYZ1/2
#define SCH16T_FILTER_ACC3 30   // Hz, LPF1 cut-off (-3dB) for Acc_XYZ3

#define SCH16T_SENSITIVITY_RATE1 1600 // LSB/dps for Rate_XYZ1 (20-bit data). Valid: 1600, 3200, 6400
#define SCH16T_SENSITIVITY_RATE2 1600 // LSB/dps for Rate_XYZ2
#define SCH16T_SENSITIVITY_ACC1 3200  // LSB/(m/s^2) for Acc_XYZ1. Valid: 3200, 6400, 12800, 25600
#define SCH16T_SENSITIVITY_ACC2 3200  // LSB/(m/s^2) for Acc_XYZ2
#define SCH16T_SENSITIVITY_ACC3 3200  // LSB/(m/s^2) for Acc_XYZ3

#define SCH16T_DECIMATION_RATE2 32 // Output sample rate divider for Rate_XYZ2. Valid: 2, 4, 8, 16, 32
#define SCH16T_DECIMATION_ACC2 32  // Output sample rate divider for Acc_XYZ2

// The sensor's Data Ready pin is wired to GYRO_DRDY but left disabled: the ADCS task polls
// the sensor rather than taking an interrupt off it.
#define SCH16T_ENABLE_DRY false

// Startup sequence timings, from section 5 ("Component Operation, Reset and Power Up") of
// the SCH16T data sheet
#define SCH16T_STARTUP_ATTEMPTS 2  // How many times to run the startup sequence before giving up
#define SCH16T_RESET_PULSE_MS 2    // How long EXTRESN is held low to reset the sensor
#define SCH16T_NVM_READ_WAIT_MS 32 // Wait after a reset for the non-volatile memory read to finish
#define SCH16T_STARTUP_WAIT_MS 215 // Wait after EN_SENSOR = 1 for the sensor to stabilise
#define SCH16T_EOI_WAIT_MS 3       // Wait after EOI = 1 before the status registers mean anything

/* ---------- SPI FRAME PROTOCOL ---------- */

// The SCH16T speaks 48-bit frames, MSB first, in SPI mode 0
#define SCH16T_FRAME_SIZE_BYTES 6

// A frame of all ones or all zeroes means the sensor did not answer at all
#define SCH16T_FRAME_ALL_ONES 0xFFFFFFFFFFFFULL
#define SCH16T_FRAME_ALL_ZEROES 0x000000000000ULL

// The value every status register reports when the sensor is healthy
#define SCH16T_STATUS_REGISTER_OK 0xFFFF

// Temperature is reported in hundredths of a degree Celsius
#define SCH16T_TEMPERATURE_LSB_PER_DEGREE 100.0f

// Marker value that puts an output channel's filter into bypass mode
#define SCH16T_FILTER_BYPASS 0

// Number of bytes needed to hold a serial number string, including its terminator
#define SCH16T_SERIAL_NUMBER_SIZE 11

/* ---------- STANDARD REQUESTS ---------- */

// Rate and acceleration
#define SCH16T_REQ_READ_RATE_X1 0x0048000000ACULL
#define SCH16T_REQ_READ_RATE_Y1 0x00880000009AULL
#define SCH16T_REQ_READ_RATE_Z1 0x00C80000006DULL
#define SCH16T_REQ_READ_ACC_X1 0x0108000000F6ULL
#define SCH16T_REQ_READ_ACC_Y1 0x014800000001ULL
#define SCH16T_REQ_READ_ACC_Z1 0x018800000037ULL
#define SCH16T_REQ_READ_ACC_X3 0x01C8000000C0ULL
#define SCH16T_REQ_READ_ACC_Y3 0x02080000002EULL
#define SCH16T_REQ_READ_ACC_Z3 0x0248000000D9ULL
#define SCH16T_REQ_READ_RATE_X2 0x0288000000EFULL
#define SCH16T_REQ_READ_RATE_Y2 0x02C800000018ULL
#define SCH16T_REQ_READ_RATE_Z2 0x030800000083ULL
#define SCH16T_REQ_READ_ACC_X2 0x034800000074ULL
#define SCH16T_REQ_READ_ACC_Y2 0x038800000042ULL
#define SCH16T_REQ_READ_ACC_Z2 0x03C8000000B5ULL

// Status
#define SCH16T_REQ_READ_STAT_SUM 0x05080000001CULL
#define SCH16T_REQ_READ_STAT_SUM_SAT 0x0548000000EBULL
#define SCH16T_REQ_READ_STAT_COM 0x0588000000DDULL
#define SCH16T_REQ_READ_STAT_RATE_COM 0x05C80000002AULL
#define SCH16T_REQ_READ_STAT_RATE_X 0x0608000000C4ULL
#define SCH16T_REQ_READ_STAT_RATE_Y 0x064800000033ULL
#define SCH16T_REQ_READ_STAT_RATE_Z 0x068800000005ULL
#define SCH16T_REQ_READ_STAT_ACC_X 0x06C8000000F2ULL
#define SCH16T_REQ_READ_STAT_ACC_Y 0x070800000069ULL
#define SCH16T_REQ_READ_STAT_ACC_Z 0x07480000009EULL

// Temperature and traceability
#define SCH16T_REQ_READ_TEMP 0x0408000000B1ULL
#define SCH16T_REQ_READ_SN_ID1 0x0F4800000065ULL
#define SCH16T_REQ_READ_SN_ID2 0x0F8800000053ULL
#define SCH16T_REQ_READ_SN_ID3 0x0FC8000000A4ULL
#define SCH16T_REQ_READ_COMP_ID 0x0F0800000092ULL

// Filters
#define SCH16T_REQ_READ_FILT_RATE 0x0948000000FAULL
#define SCH16T_REQ_READ_FILT_ACC12 0x0988000000CCULL
#define SCH16T_REQ_READ_FILT_ACC3 0x09C80000003BULL
#define SCH16T_REQ_SET_FILT_RATE 0x0968000000ULL  // Base frame for the Rate_XYZ1/2 filter setting
#define SCH16T_REQ_SET_FILT_ACC12 0x09A8000000ULL // Base frame for the Acc_XYZ1/2 filter setting
#define SCH16T_REQ_SET_FILT_ACC3 0x09E8000000ULL  // Base frame for the Acc_XYZ3 filter setting

// Sensitivity and decimation
#define SCH16T_REQ_READ_RATE_CTRL 0x0A08000000D5ULL
#define SCH16T_REQ_READ_ACC12_CTRL 0x0A4800000022ULL
#define SCH16T_REQ_READ_ACC3_CTRL 0x0A8800000014ULL
#define SCH16T_REQ_READ_MODE_CTRL 0x0D4800000010ULL
#define SCH16T_REQ_SET_RATE_CTRL 0x0A28000000ULL  // Base frame for Rate_XYZ1/2 sensitivity and Rate_XYZ2 decimation
#define SCH16T_REQ_SET_ACC12_CTRL 0x0A68000000ULL // Base frame for Acc_XYZ1/2 sensitivity and Acc_XYZ2 decimation
#define SCH16T_REQ_SET_ACC3_CTRL 0x0AA8000000ULL  // Base frame for the Acc_XYZ3 sensitivity
#define SCH16T_REQ_SET_MODE_CTRL 0x0D68000000ULL  // Base frame for the MODE register

// DRY/SYNC configuration
#define SCH16T_REQ_READ_USER_IF_CTRL 0x0CC80000007CULL
#define SCH16T_REQ_SET_USER_IF_CTRL 0x0CE8000000ULL // Base frame for the USER_IF_CTRL register

// Other
#define SCH16T_REQ_SOFTRESET 0x0DA800000AC3ULL // SPI soft reset command

/* ---------- FRAME FIELD MASKS ---------- */

#define SCH16T_TA_FIELD_MASK 0xFFC000000000ULL    // Target Address, set on requests
#define SCH16T_SA_FIELD_MASK 0x7FE000000000ULL    // Source Address, set on responses
#define SCH16T_DATA_FIELD_MASK 0x00000FFFFF00ULL  // 20-bit payload
#define SCH16T_CRC_FIELD_MASK 0x0000000000FFULL   // CRC8 over the other 40 bits
#define SCH16T_ERROR_FIELD_MASK 0x001E00000000ULL // Set by the sensor when a frame is not trustworthy

// Amount each address field must be shifted down by to be compared against the other
#define SCH16T_TA_FIELD_SHIFT 38
#define SCH16T_SA_FIELD_SHIFT 37

// Only the eight register-address bits (TA7..TA0) are compared when checking that a response
// came from the register that was asked for. Whether the sensor echoes the TA9/TA8 select bits
// below back in the Source Address is not something this port can confirm without the data
// sheet, so they are left out of the comparison rather than risking a false mismatch.
#define SCH16T_TA_REGISTER_MASK 0xFF

/* ---------- TARGET ADDRESS SELECT (TA9/TA8) ---------- */

// The Target Address field is 10 bits wide. Its top two bits, TA9 and TA8, are driven by
// physical pins on the sensor and act as a device select, letting several SCH16Ts share one bus
// without separate chip-select lines. They were left unconnected on the Arduino prototype's
// breakout, so every request there carried TA9 = TA8 = 0 -- which is what the CRC baked into
// each SCH16T_REQ_* constant above assumes.
//
// Set this to the two-bit value the board straps TA9/TA8 to. Because the CRC covers the Target
// Address, changing it makes gyro_apply_ta_select() recompute each frame's CRC.
#define SCH16T_TA_SELECT 0x3 // Valid: 0x0, 0x1, 0x2, 0x3 (TA9 is the high bit)

// TA9/TA8 sit at frame bits 47 and 46, i.e. the top two bits of the first byte on the wire
#define SCH16T_TA_SELECT_SHIFT 46
#define SCH16T_TA_SELECT_MASK ((uint64_t)(SCH16T_TA_SELECT & 0x3) << SCH16T_TA_SELECT_SHIFT)

/* ---------- MODE_CTRL AND USER_IF_CTRL BITS ---------- */

#define SCH16T_MODE_EN_SENSOR 0x01 // Enables the sensor
#define SCH16T_MODE_EOI_CTRL 0x02  // End Of Initialization; locks every R/W register but the soft reset

#define SCH16T_USER_IF_DRY_POLARITY 0x40 // 0 = DRY active high, 1 = DRY active low
#define SCH16T_USER_IF_DRY_ENABLE 0x20   // Enables the DRY pin

/* ---------- PAYLOAD EXTRACTION ---------- */

// Sign-extend the 20-bit payload of a 48-bit frame into an int32_t
#define SCH16T_SPI48_DATA_INT32(a) (((int32_t)(((a) << 4) & 0xfffff000UL)) >> 12)
// Take the payload of a 48-bit frame as an unsigned 16-bit register value
#define SCH16T_SPI48_DATA_UINT16(a) ((uint16_t)(((a) >> 8) & 0x0000ffffUL))

/* ---------- GPIO HELPERS ---------- */

// Functions for driving the gyro's chip-select and (active-low) EXTRESN reset pins
#define GYRO_CS_LOW() gpio_set_pin_level(GYRO_CS, 0)
#define GYRO_CS_HIGH() gpio_set_pin_level(GYRO_CS, 1)
#define GYRO_RST_LOW() gpio_set_pin_level(GYRO_RST, 0)
#define GYRO_RST_HIGH() gpio_set_pin_level(GYRO_RST, 1)

/* ---------- DATA TYPES ---------- */

// SCH1 filter corner frequencies, in Hz
typedef struct {
    uint16_t Rate12;
    uint16_t Acc12;
    uint16_t Acc3;
} SCH1_filter_t;

// SCH1 channel sensitivities, in LSB/dps for rate and LSB/(m/s^2) for acceleration
typedef struct {
    uint16_t Rate1;
    uint16_t Rate2;
    uint16_t Acc1;
    uint16_t Acc2;
    uint16_t Acc3;
} SCH1_sensitivity_t;

// SCH1 output sample rate dividers for the decimated channels
typedef struct {
    uint16_t Rate2;
    uint16_t Acc2;
} SCH1_decimation_t;

// SCH1 status registers. Every field reads SCH16T_STATUS_REGISTER_OK when the sensor is healthy.
typedef struct {
    uint16_t Summary;
    uint16_t Summary_Sat;
    uint16_t Common;
    uint16_t Rate_Common;
    uint16_t Rate_X;
    uint16_t Rate_Y;
    uint16_t Rate_Z;
    uint16_t Acc_X;
    uint16_t Acc_Y;
    uint16_t Acc_Z;
} SCH1_status_t;

// SCH1 raw, unscaled sensor counts.
// NOTE: only Rate1_raw, Acc1_raw and Temp_raw are populated; the decimated (Rate2, Acc2) and
// Acc3 channels are configured but never read, matching the Arduino prototype this was ported
// from. Add them to gyro_measurement_requests in gyro_driver.c if ADCS ever needs them.
typedef struct {
    int32_t Rate1_raw[3];
    int32_t Rate2_raw[3];
    int32_t Acc1_raw[3];
    int32_t Acc2_raw[3];
    int32_t Acc3_raw[3];
    int32_t Temp_raw;
    bool frame_error;
} SCH1_raw_data_t;

// SCH1 scaled measurement results: rate in dps, acceleration in m/s^2, temperature in degrees
// Celsius. Indices are x, y, z. Only Rate1, Acc1 and Temp are populated; see SCH1_raw_data_t.
typedef struct {
    float Rate1[3];
    float Rate2[3];
    float Acc1[3];
    float Acc2[3];
    float Acc3[3];
    float Temp;
} SCH1_result_t;

/* ---------- FUNCTIONS ---------- */

status_t init_gyro_hardware(void);
void gyro_reset(void);
status_t gyro_read(SCH1_result_t *const result);
status_t gyro_get_status(SCH1_status_t *const p_status);
bool gyro_status_ok(const SCH1_status_t *const p_status);
status_t gyro_get_serial_number(char *const p_buffer, size_t buffer_size);

#endif // GYRO_DRIVER_H
