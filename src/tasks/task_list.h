#ifndef TASK_LIST_H
#define TASK_LIST_H

// Includes
#include "globals.h"

// Extern defs of task pointers which can be accessed throughout the PVDXos codebase
extern pvdx_task_t *const p_watchdog_task;
extern pvdx_task_t *const p_command_dispatcher_task;
extern pvdx_task_t *const p_task_manager_task;
extern pvdx_task_t *const p_adcs_task;
extern pvdx_task_t *const p_shell_task;
extern pvdx_task_t *const p_display_task;
extern pvdx_task_t *const p_heartbeat_task;
extern pvdx_task_t *task_list[];

pvdx_task_t *get_current_task(void);
TickType_t get_command_queue_block_time_ticks(pvdx_task_t *const task);
bool should_checkin(pvdx_task_t *const p_task);

#endif // TASK_LIST_H
