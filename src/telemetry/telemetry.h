#ifndef PVDX_TELEMETRY_H
#define PVDX_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Reference: PVDX UHF Uplink/Downlink Summary 3/3/2026
// (https://docs.google.com/spreadsheets/d/1XYJBU4V2vjlKj8QtCxaPuW247gf_tx6XY4dey5q0v0k/edit?usp=sharing)
// Every multi-byte value is sent most significant byte first, and floats are sent as their IEEE 754 bits

#define TELEMETRY_CALLSIGN_LENGTH 6
#define TELEMETRY_CALLSIGN "BSEBSE" // TODO: Make real value
#define TELEMETRY_DOWNLINK_APID 1   // TODO: Make real value

#define TELEMETRY_FALSE 0x00
#define TELEMETRY_TRUE 0x01

/* ---------- DOWNLINK ---------- */

// Size of a serialized downlink packet, including its SPP primary and secondary headers. Bytes after the
// last field are spare, and are sent as zeros
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
    int32_t current[3]; // "current" applied to the x, y, z magnetorquers (really PWM frequency)
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
    // Sent in the SPP headers. The callsign is always TELEMETRY_CALLSIGN
    uint16_t sequence_count; // only the low 14 bits are sent, so it wraps at 0x4000
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
 * Serializes a downlink packet: an SPP telemetry packet with a secondary header, whose user data field
 * holds every field of downlink, in order
 *
 * \param downlink - telemetry to send
 * \param buf - buffer to write into; exactly TELEMETRY_DOWNLINK_SIZE bytes are written on success
 * \param buf_size - size of buf, in bytes
 *
 * \return true on failure (NULL args or buf_size < TELEMETRY_DOWNLINK_SIZE), else false
 */
bool telemetry_downlink_serialize(const tel_downlink_t *downlink, uint8_t *buf, size_t buf_size);

/* ---------- UPLINK ---------- */

#define TELEMETRY_UPLINK_MAX_COMMANDS 16
#define TELEMETRY_DISPLAY_BITMAP_SIZE 8192 // 256 x 64 pixels, 4 bits (16 levels) each
#define TELEMETRY_KEPLER_COEFFICIENTS 6

// Command IDs, sent as 2 bytes
typedef enum {
    TEL_CMD_ENABLE_DISABLE_DEVICE = 0,
    TEL_CMD_SET_DEVICE_BROKEN = 1,
    TEL_CMD_UPDATE_DISPLAY = 2,
    TEL_CMD_SLEEP = 3,
    TEL_CMD_REBOOT = 4,
    TEL_CMD_TAKE_PICTURE = 5,
    TEL_CMD_SEND_PICTURES = 6,
    TEL_CMD_SET_KEPLER_COEFFICIENTS = 7,
    TEL_CMD_SET_UNIX_TIME = 8,
    TEL_CMD_SET_ADCS_OPMODE = 9,
    TEL_CMD_ADCS_UPDATE_PARAMETERS = 10,
    TEL_CMD_SET_POWER_MODE = 11,
    TEL_CMD_COUNT, // not a command
} tel_command_id_t;

// Data for TEL_CMD_ADCS_UPDATE_PARAMETERS
typedef struct {
    uint32_t timestamp;
    float inclination;         // degrees, angle from equatorial reference plane
    float raan;                // degrees, measured in ECI
    float eccentricity;        // unitless, 0-1
    float argument_of_perigee; // degrees
    float mean_anomaly;
    float mean_motion; // revolutions per day
} tel_adcs_parameters_t;

typedef struct {
    tel_command_id_t id;
    uint32_t data_size;  // bytes of command data
    const uint8_t *data; // raw command data; points into the parsed buffer
    // Command data decoded according to id. TEL_CMD_UPDATE_DISPLAY has no member here; its bitmap is data
    union {
        uint16_t device; // a device_id_t; TEL_CMD_ENABLE_DISABLE_DEVICE, TEL_CMD_SET_DEVICE_BROKEN
        uint32_t sleep_time;
        uint8_t reboot;
        uint32_t picture_timestamp;
        int32_t picture_count;
        float kepler_coefficients[TELEMETRY_KEPLER_COEFFICIENTS];
        uint32_t unix_time;
        uint16_t adcs_opmode;
        tel_adcs_parameters_t adcs_parameters;
        uint16_t power_mode;
    } args;
} tel_command_t;

typedef struct {
    char callsign[TELEMETRY_CALLSIGN_LENGTH]; // not NULL-terminated
    uint32_t message_size;                    // size of the whole SPP packet, in bytes
    uint16_t num_commands;
    uint32_t timestamp; // when the uplink was sent
    tel_command_t commands[TELEMETRY_UPLINK_MAX_COMMANDS];
} tel_uplink_t;

/**
 * Parses an uplink packet: an SPP telecommand packet whose user data field holds a preamble followed by
 * num_commands commands
 *
 * - No copy (on success, each command's data points into buf, so buf must outlive uplink)
 * - Bytes in buf past the end given by the SPP packet data length field are ignored
 * - Rejects the packet if message_size isn't the SPP packet's size, if there are more than
 *   TELEMETRY_UPLINK_MAX_COMMANDS commands, if a command is unknown or has the wrong data size for its ID,
 *   or if the commands don't exactly fill the packet
 *
 * \param uplink - uplink to fill in; left unspecified on failure
 * \param buf - pointer to start of the SPP packet
 * \param len - length, in bytes, of buf
 *
 * \return true on failure (malformed or unsupported), else false
 */
bool telemetry_uplink_parse(tel_uplink_t *uplink, uint8_t *buf, size_t len);

#endif
