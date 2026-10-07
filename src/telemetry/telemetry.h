#ifndef PVDX_TELEMETRY_H
#define PVDX_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TELEMETRY_CALLSIGN_LENGTH 6
#define TELEMETRY_CALLSIGN "BSEBSE" // TODO: Make real value
#define TELEMETRY_PREAMBLE_APID 1   // TODO: Make real value
#define TELEMETRY_PACKET_SIZE 157   // For now

// Reference: PVDX UHF Uplink/Downlink Summary 3/3/2026
// (https://docs.google.com/spreadsheets/d/1XYJBU4V2vjlKj8QtCxaPuW247gf_tx6XY4dey5q0v0k/edit?usp=sharing)
typedef struct {
    char callsign[TELEMETRY_CALLSIGN_LENGTH];
    uint32_t state;
    uint32_t timestamp; // time since last boot
    uint32_t message_size;
} tel_preamble_t;

typedef struct {
    uint8_t revid_register;
    uint8_t bist_register;
    uint32_t timestamp; // time when reading was taken
    int32_t raw_readings[3];
    float gain_adjusted_readings[3];
} tel_magnetometer_data_t;

typedef struct {
    int32_t current[3];
} tel_magnetorquer_data_t;

typedef struct {
    float raw_readings[22];
} tel_photodiode_data_t;

typedef struct {
    // TODO: What is this data?
} tel_gyro_data_t;

typedef struct {
    bool display;
    bool camera;
    bool uhf_radio;
    bool sband_radio;
} tel_peripheral_status_t;

typedef struct {
    float charge;
} tel_eps_data_t;

typedef struct {
    uint16_t state; // TODO: Update with actual state data
} tel_state_machine_data_t;

typedef struct {
    uint16_t attitude;
    bool bdot_status;
    // TODO: Update and add more values
} tel_adcs_data_t;

typedef struct {
    tel_preamble_t preamble;
    tel_magnetometer_data_t magnetometer;
    tel_magnetorquer_data_t magnetorquer;
    tel_photodiode_data_t photodiode;
    tel_gyro_data_t gyro;
    tel_peripheral_status_t peripheral_status;
    tel_eps_data_t eps;
    tel_state_machine_data_t state_machine;
    tel_adcs_data_t adcs;
} telemetry_data_t;

bool serialize_telemetry(size_t buf_size, uint8_t *buf, telemetry_data_t *data); // Function to serialize all telemetry data

#endif