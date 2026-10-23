#ifndef HARDWARE_GYRO_CHECK_H
#define HARDWARE_GYRO_CHECK_H

#include <stdbool.h>

/* Bare-metal SCH16T-K01 diagnostic. Caller owns SPI_MAGNETOMETER_GYRO:
 * configure/enable mode 0, 1 MHz, MSB-first, 8-bit characters and leave both
 * sensor CS pins high before init. Calls must not overlap other SPI users.
 * Init software-resets the gyro, changes volatile controls and sets EOI (which
 * locks those controls until reset). No hardware reset GPIO or NV writes.
 * Identity is probed at all four TA9:TA8 strap settings before configuration.
 * Logging requires DEVBUILD.
 */
bool hardware_gyro_init(void);

/* Read/log one unaveraged XYZ rate, XYZ acceleration, temperature and status.
 * Returns false before successful init or on any transfer/protocol/status error.
 * No DRDY synchronization, atomic multi-axis snapshot or freshness guarantee.
 */
bool hardware_gyro_sample(void);

#endif // HARDWARE_GYRO_CHECK_H
