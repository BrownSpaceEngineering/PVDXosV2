#include "uplink_commands.h"

#include "logging.h"

static inline uint32_t big_endian_to_uint32(const uint8_t arr[4]) {
    return ((uint32_t)arr[0] << 24) | ((uint32_t)arr[1] << 16) | ((uint32_t)arr[2] << 8) | ((uint32_t)arr[3]);
}

static inline uint32_t big_endian_to_uint16(const uint8_t arr[2]) {
    return ((uint32_t)arr[2] << 8) | ((uint32_t)arr[3]);
}

int parse_uplink_packet(const uint8_t *raw, uplink_packet_t *out, size_t n) {
    if (!raw || !out) {
        return -1;
    }

    if (n < 16)
        return -2;

    out->callsign[0] = raw[5];
    out->callsign[1] = raw[4];
    out->callsign[2] = raw[3];
    out->callsign[3] = raw[2];
    out->callsign[4] = raw[1];
    out->callsign[5] = raw[0];

    out->message_size = big_endian_to_uint32(raw + 6);
    out->n_cmds = big_endian_to_uint16(raw + 10);
    out->timestamp = big_endian_to_uint32(raw + 12);

    // Note: this assumes message size does not include preamble
    if (n - 16 < out->message_size) {
        warning("unable to parse entire uplink packet, data size: %lu less than promised %lu", (n - 16), out->message_size);
        return -3;
    }

    // size_t remaining_size = out->message_size;
    // size_t remaining_cmds = out->n_cmds;

    // while (remaining_size > 0 && remaining_cmds > 0) {
    //         int ret = parse_uplink_cmd(, , );
    //}
    return 0;
}

// int parse_uplink_cmd(const uint8_t *raw, uplink_cmd_t *out, size_t n);
