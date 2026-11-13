#ifndef RAM_ECC_H
#define RAM_ECC_H

#include <stdint.h>

// Single-bit RAM errors corrected since boot
extern volatile uint32_t ram_ecc_single_errors;

void ram_ecc_init(void);

#endif // RAM_ECC_H
