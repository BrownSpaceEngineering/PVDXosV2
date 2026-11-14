#ifndef PUSP_H
#define PUSP_H
// PVDX UHF Synchronization Protocol (PUSP) Specification

#include <stdint.h>

// The ground opens a link with SYN -> SYN_ACK -> SRT, after which the ground has the turn. Whoever has the turn hands it
// over with YLD -> YLD_ACK -> SWC. The link is closed by the inactivity timeout on both sides
enum pusp_directive {
    SYN = 1,     // Ground -> sat: open a link
    SYN_ACK = 2, // Sat -> ground: SYN received
    SRT = 3,     // Ground -> sat: SYN_ACK received, link established, ground has the turn
    YLD = 4,     // Either side: I'm done sending, take the turn
    YLD_ACK = 5, // Either side: YLD received, ready to take the turn
    SWC = 6,     // Either side: YLD_ACK received, you have the turn now
};

typedef uint8_t pusp_directive_t;

typedef struct pusp_packet {
    pusp_directive_t dir : 8;
} pusp_packet_t;
#endif // PUSP_H
