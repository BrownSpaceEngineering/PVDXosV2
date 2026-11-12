#ifndef PVDX_CCSDS_H
#define PVDX_CCSDS_H

/**
 * src/ccsds/ccsds.h
 *
 * header file for the general initialization of the PVDX implementation of CCSDS utilities
 *
 * Created: 20260429 SUN
 * Updated: 202600927 THU
 * Authors: Zach Mahan, Ilan Goldfein
 */

#include "uslp.h"

#define CCSDS_SCID 0x0001

// SPP Application Process IDs (APIDs), 11 bits each. SPP_APID_IDLE_PACKET (0x7FF) is reserved, see spp.h
// TODO: confirm these values with the ground station
#define CCSDS_APID_CFDP 0x02 // CFDP PDUs
#define CCSDS_APID_PUSP 0x03 // PVDX UHF Synchronization Protocol packets, see tasks/uhf_radio/pusp.h

/// initializes all ccsds utilities
/// - should be called once, on boot and before the task loop begins
static inline void ccsds_init(void) {
    uslp_init();
}

#endif // PVDX_CCSDS_H
