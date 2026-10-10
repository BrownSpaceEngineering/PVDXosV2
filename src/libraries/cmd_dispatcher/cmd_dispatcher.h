#ifndef CMD_DISPATCHER_H
#define CMD_DISPATCHER_H

#include "globals.h"

typedef struct {
    pvdx_task_t *target;
    command_data_type_t data_type;
    operation_t operation; 
} cmd_record_t;

status_t get_n_logs(uint8_t n, cmd_record_t *logs);
status_t enqueue_command(command_t *p_cmd);

#endif // CMD_DISPATCHER_H