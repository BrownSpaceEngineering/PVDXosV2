#ifndef TASK_LIST_H
#define TASK_LIST_H

// Includes
#include "command_dispatcher_task.h"
#include "display_task.h"
#include "globals.h"
#include "heartbeat_task.h"
#include "shell_task.h"
#include "task_manager_task.h"
#include "watchdog_task.h"

// Represents the end of a pvdx_task_t array, contains all null parameters
#define NULL_TASK ((pvdx_task_t){NULL, false, NULL, NULL, 0, NULL, NULL, 0, NULL, 0, 0, NULL_TASK, 0, 0, false})

extern pvdx_task_t task_list[];

pvdx_task_t *get_task(TaskHandle_t id);
TickType_t get_command_queue_block_time_ticks(pvdx_task_t *task);

#endif // TASK_LIST_H