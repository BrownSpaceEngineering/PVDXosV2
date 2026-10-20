/**
 * at86_link_packet.h
 *
 * Packet format for the AT86RF215 <-> HackRF MR-FSK link test. scripts/hackrf_link.py packs the same layout with
 * struct format "<B6sBIB" followed by the payload. Keep the two in sync.
 *
 * The packet is the MR-FSK PSDU. The AT86RF215 appends the 2 byte FCS (CRC-16 ITU-T) after it.
 */

#ifndef AT86_LINK_PACKET_H
#define AT86_LINK_PACKET_H

#include <stdint.h>

#define LINK_CALLSIGN "NOCALL" // Exactly 6 characters, space padded. Replace with the licensed call sign before going on air
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
    uint8_t length;   // Number of bytes after this field (rest of the header + payload). Does not include the FCS
    char callsign[6]; // Not null terminated
    uint8_t ver_ssid; // Version in bits 7:4, SSID in bits 3:0
    uint32_t seq;     // Little endian
    uint8_t type;     // link_type_t
    uint8_t payload[LINK_MAX_PAYLOAD];
} link_packet_t;

#define LINK_HEADER_SIZE (sizeof(link_packet_t) - LINK_MAX_PAYLOAD) // 13: length .. type
#define LINK_LENGTH_FIELD(payload_len) ((uint8_t)(LINK_HEADER_SIZE - 1 + (payload_len)))
#define LINK_FCS_SIZE 2

_Static_assert(LINK_HEADER_SIZE == 13, "link packet header must be 13 bytes");

#endif // AT86_LINK_PACKET_H
