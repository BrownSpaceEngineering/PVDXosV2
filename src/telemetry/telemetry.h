#ifndef PVDX_TELEMETRY_H
#define PVDX_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/gyro/gyro_driver.h"
#include "drivers/magnetometer/magnetometer_driver.h"
#include "drivers/magnetorquer/magnetorquer_driver.h"

// Reference: PVDX UHF Uplink/Downlink Summary 3/3/2026
// (https://docs.google.com/spreadsheets/d/1XYJBU4V2vjlKj8QtCxaPuW247gf_tx6XY4dey5q0v0k/edit?usp=sharing)
// Every multi-byte value is sent most significant byte first, and floats are sent as their IEEE 754 bits
// The downlink is a file sent with CFDP, which wraps each PDU in an SPP packet itself, so it has no SPP headers.
// Uplinked commands are parsed in tasks/cfdp/uplink_parse.c

#define TELEMETRY_FALSE 0x00
#define TELEMETRY_TRUE 0x01

/* ---------- DOWNLINK ---------- */

// Size of a serialized downlink file. Bytes after the last field are spare, and are sent as zeros
#define TELEMETRY_DOWNLINK_SIZE 512

// Health of a device, sent as one byte
typedef enum {
    TEL_DEVICE_STATUS_OK = 0,
    TEL_DEVICE_STATUS_DISABLED,
    TEL_DEVICE_STATUS_BROKEN,
} tel_device_status_t;

typedef struct {
    uint8_t revid_register; // should not change; tells us whether we can talk to the device at all
    uint8_t bist_register;  // built-in self-test; tells us whether the device is operating nominally
    int32_t raw_readings[3];
    float gain_adjusted_readings[3]; // what is fed into the ADCS algorithms
} tel_magnetometer_data_t;

typedef struct {
    float duty[3]; // signed PWM duty applied to the x, y, z magnetorquers, from -1.0 to 1.0
} tel_magnetorquer_data_t;

typedef struct {
    float raw_readings[22];
} tel_photodiode_data_t;

typedef struct {
    int32_t raw_rate[3]; // TODO: The spreadsheet only says "approximately equivalent to magnetometer data"
    float rate[3];       // degrees per second
    tel_device_status_t health;
} tel_gyro_data_t;

typedef struct {
    tel_device_status_t health;
    bool last_capture_succeeded;
    uint8_t resolution;
} tel_camera_data_t;

typedef struct {
    tel_device_status_t health;
    uint32_t last_transmission_timestamp; // last attempted transmission
} tel_sband_data_t;

typedef struct {
    // One entry per fuel gauge
    float charge_percent[4];
    float voltage[4];
    float current[4];
    float temperature[4];
} tel_eps_data_t;

typedef struct {
    uint8_t state_history[8];  // last 8 states
    float transition_times[8]; // timestamps of the last 8 transitions
    uint8_t input_variables[3];
} tel_state_machine_data_t;

typedef struct {
    float position[3];
    float attitude[3];
    float state[7];      // error 6-vector & bias value
    float covariance[4]; // 2x2 matrix
} tel_adcs_data_t;

typedef struct {
    uint8_t status[3];
    uint32_t reflash_count;
    uint32_t reflash_timestamps[8]; // last 8 reflashes
} tel_mram_data_t;

typedef struct {
    uint8_t error_codes[8]; // last 8 OS errors
    uint32_t error_timestamps[8];
} tel_os_error_data_t;

typedef struct {
    uint32_t timestamp;      // unix time
    uint32_t boot_timestamp; // unix time of the last boot

    uint8_t last_bootloader; // bootloader copy that we last booted from
    tel_magnetometer_data_t magnetometer;
    tel_magnetorquer_data_t magnetorquer;
    tel_photodiode_data_t photodiode;
    tel_gyro_data_t gyro;
    tel_camera_data_t camera;
    tel_device_status_t display_health;
    tel_device_status_t uhf_health;
    tel_sband_data_t sband;
    tel_eps_data_t eps;
    tel_state_machine_data_t state_machine;
    tel_adcs_data_t adcs;
    uint16_t command_timestamps[16]; // sent timestamps of received commands
    tel_mram_data_t mram;
    tel_os_error_data_t os_errors;
} tel_downlink_t;

/**
 * Serializes a downlink file, to be sent with CFDP: every field of downlink, in order
 *
 * \param downlink - telemetry to send
 * \param buf - buffer to write into; exactly TELEMETRY_DOWNLINK_SIZE bytes are written on success
 * \param buf_size - size of buf, in bytes
 *
 * \return true on failure (NULL args or buf_size < TELEMETRY_DOWNLINK_SIZE), else false
 */
bool telemetry_downlink_serialize(const tel_downlink_t *downlink, uint8_t *buf, size_t buf_size);

/**
 * Fills in downlink with the current telemetry
 *
 * \param downlink - telemetry to fill in
 */
void telemetry_collect(tel_downlink_t *downlink);

/**
 * Updates the timestamp of last boot in the telemetry cache
 *
 * \param boot_timestamp - timstamp of last boot
 */
void telemetry_update_boot_timestamp(uint32_t boot_timestamp);

/**
 * Updates which bootloader was last booted from in telemetry cache
 *
 * \param bootloader - Last Bootloader number booted from
 */
void telemetry_update_last_bootloader(uint8_t bootloader);

/**
 * Updates the magnetometer registers inside the telemetry cache
 *
 * \param revid - revid register
 * \param bist - bist register
 */
void telemetry_update_magnetometer_registers(uint8_t revid, uint8_t bist);

/**
 * Updates the magnetometer readings inside the telemetry cache
 *
 * \param raw - raw magnetometer data
 * \param gain_adjusted - gain adjusted magnetometer data
 */
void telemetry_update_magnetometer_readings(const mag_raw_reading_t *raw, const mag_data_t *gain_adjusted);

/**
 * Updates the magnetorquer data inside the telemetry cache
 *
 * \param mag_data - signed PWM duty of each magnetorquer, from -1.0 to 1.0
 */
void telemetry_update_magnetorquer_data(const mag_torque_input_t *mag_data);

/**
 * Updates the photodiode data inside the telemetry cache
 *
 * \param photodiode_data - photodiode data to update
 */
void telemetry_update_photodiode_data(const float photodiode_data[22]);

/**
 * Updates the gyroscope data inside the telemetry cache
 *
 * \param gyro_readings - gyroscope data to update, or NULL if the read failed. The last good rates are then kept and
 * the gyroscope is marked as broken
 */
void telemetry_update_gyro_data(const SCH1_result_t *gyro_readings);

/**
 * Updates the camera data inside the telemetry cache
 *
 * \param camera_status - status of the camera
 * \param last_capture_succeeded - whether the last capture succeeded
 * \param resolution - resolution of the camera
 */
void telemetry_update_camera_data(tel_device_status_t camera_status, bool last_capture_succeeded, uint8_t resolution);

/**
 * Updates the display status inside the telemetry cache
 *
 * \param display_status - status of the display
 */
void telemetry_update_display_data(tel_device_status_t display_status);

/**
 * Updates the UHF status inside the telemetry cache
 *
 * \param uhf_status - status of the UHF radio
 */
void telemetry_update_uhf_data(tel_device_status_t uhf_status);

/**
 * Updates the sband data inside the telemetry cache
 *
 * \param sband_status - status of the sband
 * \param last_transmission_timestamp - timestamp of last sband transmission
 */
void telemetry_update_sband_data(tel_device_status_t sband_status, uint32_t last_transmission_timestamp);

/**
 * Updates the ADCS data inside the telemetry cache
 *
 * \param adcs_data - ADCS data to update
 */
void telemetry_update_adcs_data(const tel_adcs_data_t *adcs_data);

/**
 * Updates the eps data inside the telemetry cache
 *
 * \param pack_number - battery pack being read, from 0 to 3
 * \param charge_percent - state of charge, in percent
 * \param voltage - total pack voltage, in volts
 * \param current - current, in amps
 * \param temperature - temperature, in degrees Celsius
 */
void telemetry_update_eps_data(uint8_t pack_number, float charge_percent, float voltage, float current, float temperature);

/**
 * Updates the state data inside the telemetry cache
 *
 * TODO: Should input variables be included here? Where do those get recorded?
 *
 * \param state - current state
 * \param transition_time - time of state transition
 */

void telemetry_update_state(uint8_t state, float transition_time);

/**
 * Updates the MRAM data inside the telemetry cache
 *
 * \param mram_data - MRAM data to update
 */
void telemetry_update_mram_data(const tel_mram_data_t *mram_data);

/**
 * Adds an OS error to the telemetry cache. The oldest of the last 8 errors is dropped
 *
 * \param error_code - code of the OS error
 * \param error_timestamp - time of the OS error
 */
void telemetry_update_os_error(uint8_t error_code, uint32_t error_timestamp);

/**
 * Adds a received command to the telemetry cache. The oldest of the last 16 commands is dropped
 *
 * \param command_timestamp - sent timestamp of the received command
 */
void telemetry_update_command(uint16_t command_timestamp);

#endif
