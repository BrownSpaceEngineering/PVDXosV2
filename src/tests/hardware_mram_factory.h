#ifndef HARDWARE_MRAM_FACTORY_H
#define HARDWARE_MRAM_FACTORY_H

#include <stdbool.h>

#if BOARD_MRAM_FACTORY_INIT
/* DESTRUCTIVE, opt-in EST3000 Rev2.1 factory initialization of all three MRAMs.
 * Overwrites every 1 MiB array, OTP (including its control byte), status and NV
 * configuration; no data is saved/restored. Success leaves arrays FF, OTP 00,
 * default SPI/PMM/three-byte configuration, DCC=0 and DFIM exited.
 *
 * Caller must first initialize Atmel START (SPI_MRAM, its 12 MHz core clock,
 * 8-bit characters, MSB-first, delay and RTT logging facilities) and exclusively
 * own the MRAM bus/pins for the entire blocking call. The initializer drives
 * CS/reset pins high and uses JESD252 GPIO resets; it restores mode 0, BAUD=2
 * (2 MHz at 12 MHz core), not the caller's previous SPI settings.
 * No watchdog servicing: intended only for bare metal with watchdog disabled.
 * False means initialization stopped, possibly partially destructive; DFIM exit
 * recovery is best effort, not rollback. Inspect RTT channel 1 diagnostics.
 * No startup hook: define BOARD_MRAM_FACTORY_INIT=1 AND explicitly call this
 * function only when factory initialization is intended.
 */
bool hardware_mram_factory_initialize(void);
#endif

#endif // HARDWARE_MRAM_FACTORY_H
