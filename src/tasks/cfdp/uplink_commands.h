#ifndef UPLINK_COMMANDS_H
#define UPLINK_COMMANDS_H

#include <stddef.h>
#include <stdint.h>

#include "cfdp_task.h"
#include "uplink_parse.h"

/**
 * Parses an uplink received with CFDP and, only if the whole uplink is valid, runs its commands: ADCS and CFDP ones
 * straight away, the rest once the pass ends (uplink_commands_pass_exit()). Nothing points into data afterwards
 *
 * \param data - the uplink file, in cfdp_file_buff
 * \param n - size of the uplink, in bytes
 */
void uplink_commands_dispatch(const uint8_t *data, size_t n);

/**
 * Sends what this pass's uplinked commands change for tasks that don't run during a pass: one state machine input
 * update, the perovskite sampling resolution, transmit on/off, and a reboot last. Run by the CFDP task as it parks on
 * leaving the pass state
 */
void uplink_commands_pass_exit(void);

#endif // UPLINK_COMMANDS_H
