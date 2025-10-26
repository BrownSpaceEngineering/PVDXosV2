#ifndef TASK_LIST_H
#define TASK_LIST_H

// Includes
#include "../command_dispatcher/command_dispatcher_task.h"
// #include "camera_task.h" // Commented out to avoid type conflicts with camera_types.h
#include "../display/display_task.h"
#include "../globals.h"
#include "../heartbeat/heartbeat_task.h"
#include "../magnetometer/magnetometer_task.h"
#include "../photodiode/photodiode_task.h"
#include "../shell/shell_task.h"
#include "../task_manager/task_manager_task.h"
#include "../watchdog/watchdog_task.h"

// Extern defs of task pointers which can be accessed throughout the PVDXos codebase
extern pvdx_task_t *const p_watchdog_task;
extern pvdx_task_t *const p_command_dispatcher_task;
extern pvdx_task_t *const p_task_manager_task;
extern pvdx_task_t *const p_magnetometer_task;
extern pvdx_task_t *const p_photodiode_task;
extern pvdx_task_t *const p_camera_task;
extern pvdx_task_t *const p_shell_task;
extern pvdx_task_t *const p_display_task;
extern pvdx_task_t *const p_heartbeat_task;
extern pvdx_task_t *const p_test_one_task;
extern pvdx_task_t *const p_test_two_task;
extern pvdx_task_t *task_list[];

pvdx_task_t *get_current_task(void);
TickType_t get_command_queue_block_time_ticks(pvdx_task_t *const task);
bool should_checkin(pvdx_task_t *const p_task);

#endif // TASK_LIST_H
