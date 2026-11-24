#ifndef UPLINK_PARSE_H
#define UPLINK_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// An uplink is a file received with CFDP (so it has no SPP headers): a 16-byte preamble followed by n_cmds commands
//   preamble: callsign (6) | message_size (4) | n_cmds (2) | timestamp (4)
//   command:  type (2) | data size (4) | data (data size)
// message_size is the number of bytes after the preamble. Every multi-byte value is sent most significant byte
// first, and floats are sent as their IEEE 754 bits. The format is the "Uplink Command Format (Finalized)" sheet of
// the PVDX UHF Uplink/Downlink Summary.

#define UPLINK_PREAMBLE_SIZE 16
#define UPLINK_CMD_HEADER_SIZE 6
#define UPLINK_CALLSIGN_LENGTH 6
#define UPLINK_CALLSIGN "PVDXGS" // TODO: replace with actual callsign.

#define UPLINK_MAX_CMDS 16
#define UPLINK_DISPLAY_BITMAP_SIZE 8192 // 256 x 64 pixels, 4 bits (16 levels) each
#define UPLINK_KEPLER_COEFFICIENTS 6

// Command types, in the order of the sheet's "PVDX Uplink Commands" table. "Handling" is the sheet's column: Direct
// commands are run from their data as is, MRAM ones have their data kept in MRAM
// TODO: the sheet doesn't number the commands; these values assume they're numbered in table order from 0
typedef enum uplink_cmd_type {
    ENABLE_DISABLE_DEVICE = 0, // device (u16 device_id_t) | state (u8, 1 = enabled, 0 = disabled). Direct
    UPDATE_DISPLAY,            // bitmap (u8[UPLINK_DISPLAY_BITMAP_SIZE]). MRAM
    TAKE_IV_CURVE,             // sampling resolution (u32). Direct
    SEND_IV_CURVE,             // u8, no meaning (the sheet gives its data as N/A). Direct
    TRANSMIT_ON_OFF,           // u8, meaning not given (the sheet gives its data as N/A). Direct
    REBOOT,                    // flag to reboot (u8). TODO: the sheet doesn't give its handling
    TAKE_PHOTO,                // timestamp to take the picture at (u32). MRAM
    SEND_PHOTO,                // number of pictures to send (i32). Direct
    SET_KEPLER_COEFF,          // Kepler coefficients (f32[UPLINK_KEPLER_COEFFICIENTS]). MRAM
    SET_UNIX_TIME,             // Unix time in seconds (u32). Direct
    SET_ADCS_OP_MODE,          // tel_adcs_opmode_t (u16); other values reject the uplink. TODO: the sheet doesn't give its handling
    ADCS_UPDATE_PARAMS,        // uplink_adcs_parameters_t. MRAM
    SET_PWR_MODE,              // power mode (u16 enum). Direct. TODO: the power mode values aren't defined yet
    UPLINK_CMD_COUNT,          // not a command
} uplink_cmd_type_e;

typedef uint16_t uplink_cmd_type_t;

// Data for ADCS_UPDATE_PARAMS (40 bytes on the wire): a TLE's mean elements, in TLE line 2 order, plus its epoch and B*,
// then device settings. These are SGP4 mean elements: they must be copied from a TLE as is, not replaced with
// osculating Kepler elements.
typedef struct {
    double epoch_jd;           // TLE epoch, UTC, as a Julian date (a float32 or whole seconds would lose ~km of position)
    float inclination;         // degrees
    float raan;                // degrees, right ascension of the ascending node
    float eccentricity;        // unitless, 0-1
    float argument_of_perigee; // degrees
    float mean_anomaly;        // degrees
    float mean_motion;         // revolutions per day
    float bstar;               // B* drag term, 1/earth radii
    // TODO: the sheet calls this "one matrix for magnetometer and magnetorquer respectively" but gives 3 bytes. Encoding TBD
    int8_t device_rotation[3];
    // The last byte holds these flags, from its most significant bit down; its 3 lowest bits must be 0.
    // TODO: bit order is assumed (the sheet only gives the field order); confirm with the ground
    bool flip_magnetorquer_current[3]; // bits 7, 6, 5: x, y, z
    bool bdot_override;                // bit 4: run B-dot only, for 1(?) hour. TODO: duration not final
    bool dipole_off;                   // bit 3: turn the magnetorquers off, for 1(?) hour. TODO: duration not final
} uplink_adcs_parameters_t;

#define UPLINK_ADCS_PARAMETERS_SIZE 40 // On the wire

// Command data too big to fit in a command_t (the sheet's "MRAM" handling) is saved by uplink_commands_dispatch() as an
// MRAM record (see mram_record_load()) in MRAM_REGION_ADCS_UPLINK, for the ADCS task to read
#define UPLINK_MRAM_KEPLER_OFFSET 0x000      // float[UPLINK_KEPLER_COEFFICIENTS]
#define UPLINK_MRAM_ADCS_PARAMS_OFFSET 0x100 // uplink_adcs_parameters_t

typedef struct {
    uplink_cmd_type_t type;
    uint32_t data_size;  // bytes of command data
    const uint8_t *data; // raw command data; points into the parsed buffer
    // Command data decoded according to type. UPDATE_DISPLAY and SEND_IV_CURVE have no member here
    union {
        struct {
            uint16_t device; // a device_id_t
            uint8_t enabled; // 1 = enabled, 0 = disabled
        } device_state;      // ENABLE_DISABLE_DEVICE
        uint32_t iv_sampling_resolution;
        uint8_t transmit; // TRANSMIT_ON_OFF; meaning not given yet
        uint8_t reboot;
        uint32_t photo_timestamp;
        int32_t photo_count;
        float kepler_coefficients[UPLINK_KEPLER_COEFFICIENTS];
        uint32_t unix_time;
        uint16_t adcs_op_mode; // a tel_adcs_opmode_t
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
