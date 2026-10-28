#ifndef UPLINK_PARSE_H
#define UPLINK_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// An uplink is a file received with CFDP (so it has no SPP headers): a 16-byte preamble followed by n_cmds commands
//   preamble: callsign (6) | message_size (4) | n_cmds (2) | timestamp (4)
//   command:  type (2) | data size (4) | data (data size)
// message_size is the number of bytes after the preamble. Every multi-byte value is sent most significant byte
// first, and floats are sent as their IEEE 754 bits

#define UPLINK_PREAMBLE_SIZE 16
#define UPLINK_CMD_HEADER_SIZE 6
#define UPLINK_CALLSIGN_LENGTH 6
#define UPLINK_CALLSIGN "PVDXGS" // TODO: replace with actual callsign.

#define UPLINK_MAX_CMDS 16
#define UPLINK_DISPLAY_BITMAP_SIZE 8192 // 256 x 64 pixels, 4 bits (16 levels) each
#define UPLINK_KEPLER_COEFFICIENTS 6

typedef enum uplink_cmd_type {
    ENABLE_DISABLE_DEVICE = 0,
    SET_DEVICE_BROKEN_UNBROKEN,
    UPDATE_DISPLAY,
    SLEEP_TRANSMIT,
    DISABLE_TRANSMIT,
    REBOOT,
    TAKE_PHOTO,
    SEND_PHOTO,
    SET_KEPLER_COEFF,
    SET_UNIX_TIME,
    SET_ADCS_OP_MODE,
    ADCS_UPDATE_PARAMS,
    SET_PWR_MODE,
    UPLINK_CMD_COUNT, // not a command
} uplink_cmd_type_e;

typedef uint16_t uplink_cmd_type_t;

// Data for ADCS_UPDATE_PARAMS
typedef struct {
    uint32_t timestamp;
    float inclination;         // degrees, angle from equatorial reference plane
    float raan;                // degrees, measured in ECI
    float eccentricity;        // unitless, 0-1
    float argument_of_perigee; // degrees
    float mean_anomaly;
    float mean_motion; // revolutions per day
} uplink_adcs_parameters_t;

typedef struct {
    uplink_cmd_type_t type;
    uint32_t data_size;  // bytes of command data
    const uint8_t *data; // raw command data; points into the parsed buffer
    // Command data decoded according to type. UPDATE_DISPLAY and DISABLE_TRANSMIT have no member here
    union {
        uint16_t device; // a device_id_t; ENABLE_DISABLE_DEVICE, SET_DEVICE_BROKEN_UNBROKEN
        uint32_t sleep_time;
        uint8_t reboot;
        uint32_t photo_timestamp;
        int32_t photo_count;
        float kepler_coefficients[UPLINK_KEPLER_COEFFICIENTS];
        uint32_t unix_time;
        uint16_t adcs_op_mode;
        uplink_adcs_parameters_t adcs_parameters;
        uint16_t power_mode;
    } args;
} uplink_parsed_cmd_t;

typedef struct {
    char callsign[UPLINK_CALLSIGN_LENGTH]; // not NULL-terminated
    uint32_t message_size;                 // bytes after the preamble
    uint16_t n_cmds;
    uint32_t timestamp; // when the uplink was sent
    uplink_parsed_cmd_t cmds[UPLINK_MAX_CMDS];
} uplink_packet_t;

/**
 * Parses and validates a whole uplink, so that nothing in it is run unless all of it is good
 *
 * - No copy (on success, each command's data points into buf, so buf must outlive packet)
 * - Bytes in buf past the end given by message_size are ignored
 * - Rejects the uplink if the callsign isn't UPLINK_CALLSIGN, if message_size runs past len, if there are more
 *   than UPLINK_MAX_CMDS commands, if a command is unknown, has the wrong data size for its type or has invalid
 *   data, or if the commands don't exactly fill message_size
 *
 * \param packet - uplink to fill in; left unspecified on failure
 * \param buf - pointer to start of the uplink
 * \param len - length, in bytes, of buf
 *
 * \return true on failure (malformed or unsupported), else false
 */
bool uplink_packet_parse(uplink_packet_t *packet, const uint8_t *buf, size_t len);

#endif // UPLINK_PARSE_H
