#ifndef UPLINK_COMMANDS_H
#define UPLINK_COMMANDS_H

#include <stddef.h>
#include <stdint.h>

#include "cfdp_task.h"
#include "uplink_parse.h"

/**
 * Parses an uplink received with CFDP and, only if the whole uplink is valid, sends each of its commands to the
 * task that runs it
 *
 * Every command sent holds its own reference to buff (its data points into buff), so the receiving task must call
 * uplink_cmd_release() once it is done with the command. The caller keeps its own reference
 *
 * \param buff - CFDP buffer holding the uplink
 * \param n - size of the uplink, in bytes
 */
void uplink_commands_dispatch(cfdp_buff_header_t *buff, size_t n);

/**
 * Drops a command's reference to the CFDP buffer its data lives in. Call this once a CMD_DATA_UPLINK command has
 * been handled (or thrown away)
 */
void uplink_cmd_release(const uplink_cmd_t *uplink_cmd);

#endif // UPLINK_COMMANDS_H
