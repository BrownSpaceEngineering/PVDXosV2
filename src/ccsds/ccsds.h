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

/// initializes all ccsds utilities
/// - should be called once, on boot and before the task loop begins
static inline void ccsds_init(void) {
    uslp_init();
}

#endif // PVDX_CCSDS_H
