#ifndef UHF_LINK_PACKET_H
#define UHF_LINK_PACKET_H

#include <stdint.h>

#define LINK_CALLSIGN "CALLSIGN"
#define LINK_VERSION 1
#define LINK_SSID_AT86 1
#define LINK_SSID_HACKRF 2
#define LINK_MAX_PAYLOAD 32

#define LINK_VER_SSID(ver, ssid) ((uint8_t)((((ver) & 0xF) << 4) | ((ssid) & 0xF)))
#define LINK_VER(ver_ssid) (((ver_ssid) >> 4) & 0xF)
#define LINK_SSID(ver_ssid) ((ver_ssid) & 0xF)

typedef enum {
    LINK_TYPE_DATA = 0,
    LINK_TYPE_ACK = 1,
} link_type_t;

typedef struct __attribute__((packed)) {
    uint8_t length;
    char callsign[6];
    uint8_t ver_ssid;
    uint32_t seq;
    uint8_t type;
    uint8_t payload[LINK_MAX_PAYLOAD];
} link_packet_t;

#define LINK_HEADER_SIZE (sizeof(link_packet_t) - LINK_MAX_PAYLOAD)
#define LINK_LENGTH_FIELD(payload_len) ((uint8_t)(LINK_HEADER_SIZE - 1 + (payload_len)))
#define LINK_FCS_SIZE 2

_Static_assert(LINK_HEADER_SIZE == 13, "link packet header must be 13 bytes");
_Static_assert(sizeof(LINK_CALLSIGN) == 7, "LINK_CALLSIGN must be a 6 character string, padded with spaces if shorter");

#endif // UHF_LINK_PACKET_H
