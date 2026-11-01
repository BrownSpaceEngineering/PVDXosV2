#ifndef BURN_SBAND_H
#define BURN_SBAND_H

// Transmits the station identification pattern on S-band for SBAND_TX_SECONDS, then leaves the radio powered off.
// Compiled out unless SBAND_TX_TEST and DEVBUILD are both defined (see globals.h).
void sband_tx_test(void);

#endif // BURN_SBAND_H
