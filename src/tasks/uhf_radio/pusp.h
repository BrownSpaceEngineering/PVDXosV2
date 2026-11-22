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
    NTF = 7,     // Ground -> sat: carries only notification flags, no protocol meaning. Accepted in any state
};

typedef uint8_t pusp_directive_t;

typedef struct pusp_packet {
    pusp_directive_t dir : 8;
    uint8_t notifs : 8;
} pusp_packet_t;

// PUSP notification flags, in pusp_packet_t.notifs. Every directive from the ground carries them and the satellite acts
// on them whatever the directive (see dispatch_pusp_immediate_cmd()). The satellite always sends 0
#define PUSP_NOTIF_DISABLE_TX (1u << 0) // Stop transmitting: the satellite only listens until PUSP_NOTIF_ENABLE_TX
#define PUSP_NOTIF_ENABLE_TX (1u << 1)  // Transmitting allowed again. Setting both bits at once is ignored
#define PUSP_NOTIF_KNOWN (PUSP_NOTIF_DISABLE_TX | PUSP_NOTIF_ENABLE_TX) // Bits 2-7 are unused

_Static_assert(sizeof(pusp_packet_t) == 2, "the PUSP payload is two bytes");
#endif // PUSP_H
