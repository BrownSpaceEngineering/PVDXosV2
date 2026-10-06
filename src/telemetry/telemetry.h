#ifndef PVDX_TELEMETRY_H
#define PVDX_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TELEMETRY_CALLSIGN_LENGTH 6
#define TELEMETRY_CALLSIGN "BSEBSE"    // TODO: Make real value
#define TELEMETRY_PREAMBLE_APID 1      // TODO: Make real value
#define TELEMETRY_PACKET_SIZE (18 + 6) // preamble size + spp header

// Reference: PVDX UHF Uplink/Downlink Summary 3/3/2026
// (https://docs.google.com/spreadsheets/d/1XYJBU4V2vjlKj8QtCxaPuW247gf_tx6XY4dey5q0v0k/edit?usp=sharing)
typedef struct {
    char callsign[TELEMETRY_CALLSIGN_LENGTH];
    uint32_t state;
    uint32_t timestamp;
    uint32_t message_size;
} preamble_t;

bool serialize_telemetry(); // Function to serialize all telemetry data

#endif