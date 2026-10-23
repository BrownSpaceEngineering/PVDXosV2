#ifndef HARDWARE_SMOKE_CHECK_H
#define HARDWARE_SMOKE_CHECK_H

// Five bare-metal diagnostic cycles; no MRAM writes or fuel-gauge transactions.
void hardware_smoke_check(void);

#endif
