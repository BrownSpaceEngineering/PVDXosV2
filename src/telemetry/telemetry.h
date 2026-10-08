#ifndef PVDX_TELEMETRY_H
#define PVDX_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ccsds/spp.h"

#define TELEMETRY_CALLSIGN_LENGTH 6
#define TELEMETRY_CALLSIGN "BSEBSE" // TODO: Make real value
#define TELEMETRY_PREAMBLE_APID 1   // TODO: Make real value
#define TELEMETRY_PACKET_SIZE 512   // Every packet is padded to this size, per the downlink sheet
#define TELEMETRY_DATA_SIZE 458     // Sum of every section below (SPP headers included)
#define TELEMETRY_RESERVED_SIZE (TELEMETRY_PACKET_SIZE - TELEMETRY_DATA_SIZE) // zeros at the end, for fields added later

#define TELEMETRY_FALSE 0x00
#define TELEMETRY_TRUE 0x01

// Reference: PVDX UHF Uplink/Downlink Summary, "Downlink structure" tab
// (https://docs.google.com/spreadsheets/d/1XYJBU4V2vjlKj8QtCxaPuW247gf_tx6XY4dey5q0v0k/edit?usp=sharing)

typedef struct {
    uint8_t last_bootloader; // which bootloader copy was last booted from
} tel_boot_data_t;

typedef struct {
    uint8_t revid_register;
    uint8_t bist_register;
    int32_t raw_readings[3];
    float gain_adjusted_readings[3];
} tel_magnetometer_data_t;

typedef struct {
    int32_t current[3];
} tel_magnetorquer_data_t;

typedef struct {
    float raw_readings[22];
} tel_photodiode_data_t;

// The gyro has no adjusted readings, so this is the magnetometer layout without them
// TODO: confirm these fields with the ADCS team
typedef struct {
    uint16_t status;         // SCH1_status_t.Summary
    int32_t raw_readings[3]; // SCH1_raw_data_t.Rate1_raw
    uint8_t health_status;   // device status enum
} tel_gyro_data_t;

typedef struct {
    uint8_t health_status; // device status enum
    bool recent_capture_status;
    uint8_t resolution;
} tel_camera_data_t;

typedef struct {
    uint8_t health_status; // device status enum
} tel_display_data_t;

typedef struct {
    uint8_t health_status; // device status enum
} tel_uhf_data_t;

typedef struct {
    uint8_t health_status; // device status enum
    uint32_t last_transmission_timestamp;
} tel_sband_data_t;

typedef struct {
    float charge[4];
    float voltage[4];
    float current[4];
    float temperature[4];
} tel_eps_data_t;

typedef struct {
    uint8_t state_history[8];     // last 8 states
    float transition_times[8];    // timestamps of the last 8 transitions
    uint8_t input_variables[3];
} tel_state_machine_data_t;

typedef struct {
    float position[3];
    float attitude[3];
    float estimated_state[7];
    float covariance_matrix[4];
} tel_adcs_data_t;

typedef struct {
    uint16_t received_timestamps[16]; // sent timestamps of commands received
} tel_commands_data_t;

typedef struct {
    uint8_t mram_status[3];
    uint32_t total_reflashes;
    uint32_t reflash_timestamps[8]; // last 8 reflashes
} tel_mram_data_t;

typedef struct {
    uint8_t error_codes[8];       // last 8 OS error codes
    uint32_t error_timestamps[8]; // last 8 OS error timestamps
} tel_os_errors_data_t;

typedef struct {
    spp_primary_packet_header_t spp_primary;
    spp_secondary_packet_header_t spp_secondary;
    tel_boot_data_t boot;
    tel_magnetometer_data_t magnetometer;
    tel_magnetorquer_data_t magnetorquer;
    tel_photodiode_data_t photodiode;
    tel_gyro_data_t gyro;
    tel_camera_data_t camera;
    tel_display_data_t display;
    tel_uhf_data_t uhf;
    tel_sband_data_t sband;
    tel_eps_data_t eps;
    tel_state_machine_data_t state_machine;
    tel_adcs_data_t adcs;
    tel_commands_data_t commands;
    tel_mram_data_t mram;
    tel_os_errors_data_t os_errors;
} telemetry_data_t;

/// Serialize all telemetry data
bool serialize_telemetry(size_t buf_size, uint8_t *buf, telemetry_data_t *data);

#endif
