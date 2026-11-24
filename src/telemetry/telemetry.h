#ifndef PVDX_TELEMETRY_H
#define PVDX_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ccsds/spp.h"
#include "drivers/gyro/gyro_driver.h"
#include "drivers/magnetometer/magnetometer_driver.h"
#include "drivers/magnetorquer/magnetorquer_driver.h"
#include "drivers/mram/mram_driver.h"
#include "drivers/photodiode/photodiode_driver.h"
#include "states.h"

// Reference: PVDX UHF Uplink/Downlink Summary 3/3/2026
// (https://docs.google.com/spreadsheets/d/1XYJBU4V2vjlKj8QtCxaPuW247gf_tx6XY4dey5q0v0k/edit?usp=sharing)
// Every multi-byte value is sent most significant byte first, and floats are sent as their IEEE 754 bits
// The downlink is sent without CFDP: one serialized downlink is the user data of one SPP packet (CCSDS_APID_TELEMETRY),
// which fits in one USLP frame and one AT86 frame (see uhf_beacon_prepare()). The packet's secondary header carries the
// callsign, timestamp and boot timestamp, so the downlink itself doesn't.
// Uplinked commands are parsed in tasks/cfdp/uplink_parse.c

#define TELEMETRY_FALSE 0x00
#define TELEMETRY_TRUE 0x01

/* ---------- DOWNLINK ---------- */

// Size of the whole telemetry space packet, SPP headers included (sheet)
#define TELEMETRY_PACKET_SIZE 512
// Size of a serialized downlink: the packet's user data. Bytes after the last field are spare, and are sent as zeros
#define TELEMETRY_DOWNLINK_SIZE (TELEMETRY_PACKET_SIZE - SPP_PRIMARY_HEADER_SIZE - SPP_SECONDARY_HEADER_SIZE)

// Health of a device, sent as one byte
typedef enum {
    TEL_DEVICE_STATUS_OK = 0,
    TEL_DEVICE_STATUS_DISABLED,
    TEL_DEVICE_STATUS_BROKEN,
} tel_device_status_t;

typedef struct {
    uint8_t revid_register; // should not change; tells us whether we can talk to the device at all
    uint8_t bist_register;  // BIST register after the startup self test: bits 4-6 are XOK/YOK/ZOK. 0 if it couldn't run
    int32_t raw_readings[3];
    float gain_adjusted_readings[3]; // what is fed into the ADCS algorithms
} tel_magnetometer_data_t;

typedef struct {
    float duty[3]; // signed PWM duty applied to the x, y, z magnetorquers, from -1.0 to 1.0 (sheet: int32 "current";
                   // the OS commands a duty)
} tel_magnetorquer_data_t;

typedef struct {
    uint16_t raw_readings[PHD_COUNT]; // raw ADC codes, indexed by PHD number. Sheet says 22 but only 18 photodiodes
} tel_photodiode_data_t;

typedef struct {
    int32_t raw_rate[3]; // Rate1 in raw counts (SCH16T_SENSITIVITY_RATE1 per dps). The sheet's 26 bytes match the
                         // magnetometer's, but the gyro has no RevID/BIST registers, so it sends 25 (with health)
    float rate[3];       // degrees per second. Both keep the last good reading while health isn't OK
    tel_device_status_t health;
} tel_gyro_data_t;

typedef struct {
    tel_device_status_t health;
    bool last_capture_succeeded;
    uint8_t resolution; // an arducam_resolution_t
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
    uint8_t state_history[8];     // last 8 states
    uint32_t transition_times[8]; // unix timestamps of the last 8 transitions (sheet: float32, which can't hold them)
    uint8_t input_variables[3];   // TEL_INPUT_* bits: bit n is input_variables[n / 8] bit n % 8
} tel_state_machine_data_t;

// Bits of tel_state_machine_data_t.input_variables: the boolean state machine inputs (input_t in states.h)
typedef enum {
    TEL_INPUT_IN_SUNLIGHT = 0,
    TEL_INPUT_DETUMBLING,
    TEL_INPUT_IMAGE_READY_TO_DISPLAY,
    TEL_INPUT_DISPLAY_ACTIVE,
    TEL_INPUT_PHOTO_TAKEN_FOR_DOWNLINK,
    TEL_INPUT_ADCS_POINTING,
    TEL_INPUT_ADCS_SENSOR_FAULT,
    TEL_INPUT_PEROVSKITE_READY_FOR_DOWNLINK,
    TEL_INPUT_STUDENT_CAPTURE_OVERRIDE,
    TEL_INPUT_UTC_VALID,
    TEL_INPUT_SBAND_PASS_PENDING,
    TEL_INPUT_UHF_PASS_PENDING,
    TEL_INPUT_SAFE_TO_DISABLE_RADIO,
    TEL_INPUT_COUNT, // not an input; at most 24
} tel_input_bit_t;

// ADCS operating mode, sent as 2 bytes in the SET_ADCS_OP_MODE uplink command and as 1 byte in the downlink
typedef enum {
    TEL_ADCS_OPMODE_POINT = 0,         // point whenever possible, detumble (B-dot) whenever not pointing; the default
    TEL_ADCS_OPMODE_DETUMBLE_ONLY = 1, // B-dot only; the attitude filter doesn't run
    TEL_ADCS_OPMODE_COUNT,             // not a mode
} tel_adcs_opmode_t;

// Bits of tel_adcs_data_t.flags
#define TEL_ADCS_FLAG_POINTING (1u << 0)
#define TEL_ADCS_FLAG_ATTITUDE_VALID (1u << 1)
#define TEL_ADCS_FLAG_POSITION_VALID (1u << 2)
#define TEL_ADCS_FLAG_IN_SUNLIGHT (1u << 3)
#define TEL_ADCS_FLAG_TLE_LOADED (1u << 4)
#define TEL_ADCS_FLAG_UTC_VALID (1u << 5)

typedef struct {
    float position[3];            // ECI, meters, from the last SGP4 propagation; zero if TEL_ADCS_FLAG_POSITION_VALID is clear
    float attitude[3];            // body-to-ECI rotation vector (axis * angle), radians; zero if TEL_ADCS_FLAG_ATTITUDE_VALID is clear
    float gyro_bias[3];           // estimated gyro bias, rad/s
    float covariance_diag[6];     // filter covariance diagonal: attitude error variances (rad^2), then gyro bias variances
    uint8_t opmode;               // a tel_adcs_opmode_t
    uint8_t flags;                // TEL_ADCS_FLAG_* bits
    uint16_t filter_resets;       // times the filter has diverged and fallen back to detumbling since boot
    uint32_t tle_epoch_unix_time; // epoch of the TLE in use, truncated to whole seconds; 0 if none is loaded
} tel_adcs_data_t;

typedef struct {
    mram_status_t status;           // 2 bits per MRAM chip (MRAM_STATE_*), packed as by set_mram_state() (sheet: uint8[3])
    uint32_t reflash_count;         // boots on which the bootloader repaired flash from MRAM, kept in MRAM by main.c
    uint32_t reflash_timestamps[8]; // last 8 reflashes, newest first: the last UTC saved before each one's reset
} tel_mram_data_t;

typedef struct {
    uint8_t error_codes[8]; // last 8 OS errors
    uint32_t error_timestamps[8];
} tel_os_error_data_t;

typedef struct {
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
    uint32_t command_timestamps[16]; // sent timestamps of received commands (sheet: uint16, which can't hold them)
    tel_mram_data_t mram;
    tel_os_error_data_t os_errors;
} tel_downlink_t;

/**
 * Serializes a downlink: every field of downlink, in order
 *
 * \param downlink - telemetry to send
 * \param buf - buffer to write into; exactly TELEMETRY_DOWNLINK_SIZE bytes are written on success
 * \param buf_size - size of buf, in bytes
 *
 * \return true on failure (NULL args or buf_size < TELEMETRY_DOWNLINK_SIZE), else false
 */
bool telemetry_downlink_serialize(const tel_downlink_t *downlink, uint8_t *buf, size_t buf_size);

/**
 * Fills in downlink with the current telemetry, and stamps it with the current UTC and the UTC of the last boot
 * (both 0 until UTC is set)
 *
 * \param downlink - telemetry to fill in
 */
void telemetry_collect(tel_downlink_t *downlink);

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
 * \param bist - bist register, as read after the RM3100's built-in self test; 0 if it couldn't run
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
 * \param photodiode_data - raw ADC codes, as from phd_read_all()
 */
void telemetry_update_photodiode_data(const uint16_t photodiode_data[PHD_COUNT]);

/**
 * Updates the gyroscope data inside the telemetry cache
 *
 * \param gyro_readings - gyroscope data to update, or NULL if the read failed. The last good rates are then kept and
 * the gyroscope is marked as broken
 */
void telemetry_update_gyro_data(const SCH1_result_t *gyro_readings);

/**
 * Updates the camera data inside the telemetry cache from the result of a capture. SUCCESS is a good capture and OK
 * health, ERROR_TASK_DISABLED a failed capture and DISABLED health, and anything else a failed capture and BROKEN health
 *
 * \param capture_result - what camera_capture() returned
 * \param resolution - resolution of the capture, an arducam_resolution_t
 */
void telemetry_update_camera_data(status_t capture_result, uint8_t resolution);

/**
 * Updates the display status inside the telemetry cache
 *
 * \param display_status - status of the display
 */
void telemetry_update_display_data(tel_device_status_t display_status);

/**
 * Updates the display status inside the telemetry cache from the result of a display command. SUCCESS is OK health,
 * ERROR_TASK_DISABLED DISABLED, and anything else BROKEN
 *
 * \param result - what the display command returned
 */
void telemetry_update_display_result(status_t result);

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
 * \param state - current state
 * \param transition_time - unix time of state transition
 */
void telemetry_update_state(uint8_t state, uint32_t transition_time);

/**
 * Updates the state machine inputs inside the telemetry cache, packed as TEL_INPUT_* bits. Only the boolean inputs are
 * sent; the battery level, times and ticks aren't
 *
 * \param inputs - the state machine's inputs
 */
void telemetry_update_state_inputs(const input_t *inputs);

/**
 * Updates the MRAM chips' status inside the telemetry cache
 *
 * \param status - what mram_map_status() returned
 */
void telemetry_update_mram_status(mram_status_t status);

/**
 * Updates the reflash history inside the telemetry cache
 *
 * \param count - boots on which the bootloader repaired flash from MRAM
 * \param timestamps - unix times of the last 8 of them, newest first
 */
void telemetry_update_reflashes(uint32_t count, const uint32_t timestamps[8]);

/**
 * Adds an OS error to the telemetry cache. The oldest of the last 8 errors is dropped
 *
 * \param error_code - code of the OS error
 * \param error_timestamp - time of the OS error
 *
 * TODO: NOT CALLED YET: there is no one-byte error code scheme behind fatal()/warning()
 */
void telemetry_update_os_error(uint8_t error_code, uint32_t error_timestamp);

/**
 * Adds a received command to the telemetry cache. The oldest of the last 16 commands is dropped
 *
 * \param command_timestamp - sent timestamp of the received command
 */
void telemetry_update_command(uint32_t command_timestamp);

#endif
