#ifndef PUSP_H
#define PUSP_H
// PVDX UHF Synchronization Protocol (PUSP) Specification

#include <stdint.h>

enum pusp_directive {
    SYN = 1,
    SYN_ACK = 2,
    SRT = 3,
};

typedef uint8_t pusp_directive_t;

typedef struct pusp_packet {
    pusp_directive_t dir : 8;
} pusp_packet_t;
#endif // PUSP_H
