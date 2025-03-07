/**
 * task_list.c
 *
 * Global list of all tasks running on PVDXos. This list is used to initialize all tasks in the system
 * and to provide a way to access task information given a FreeRTOS task handle.
 *
 * Created: January 24, 2025
 * Authors: Oren Kohavi, Tanish Makadia, Yi Liu, Siddharta Laloux
 */

#include "task_list.h"

// Define task structs; tasks are mutable so not constant
pvdx_task_t watchdog_task = {.name = "Watchdog",
                             .enabled = true,
                             .handle = NULL,
                             .command_queue = NULL,
                             .init = init_watchdog,
                             .function = main_watchdog,
                             .stack_size = WATCHDOG_TASK_STACK_SIZE,
                             .stack_buffer = watchdog_mem.watchdog_task_stack,
                             .pvParameters = NULL,
                             .priority = 3,
                             .task_tcb = &watchdog_mem.watchdog_task_tcb,
                             .watchdog_timeout_ms = 10000,
                             .last_checkin_time_ticks = 0xDEADBEEF,
                             .has_registered = false,
                             .task_type = OS};
pvdx_task_t command_dispatcher_task = {.name = "CommandDispatcher",
                                       .enabled = true,
                                       .handle = NULL,
                                       .command_queue = NULL,
                                       .init = init_command_dispatcher,
                                       .function = main_command_dispatcher,
                                       .stack_size = COMMAND_DISPATCHER_TASK_STACK_SIZE,
                                       .stack_buffer = command_dispatcher_mem.command_dispatcher_task_stack,
                                       .pvParameters = NULL,
                                       .priority = 4,
                                       .task_tcb = &command_dispatcher_mem.command_dispatcher_task_tcb,
                                       .watchdog_timeout_ms = 10000,
                                       .last_checkin_time_ticks = 0xDEADBEEF,
                                       .has_registered = false,
                                       .task_type = OS};
pvdx_task_t task_manager_task = {.name = "TaskManager",
                                 .enabled = true,
                                 .handle = NULL,
                                 .command_queue = NULL,
                                 .init = init_task_manager,
                                 .function = main_task_manager,
                                 .stack_size = TASK_MANAGER_TASK_STACK_SIZE,
                                 .stack_buffer = task_manager_mem.task_manager_task_stack,
                                 .pvParameters = NULL,
                                 .priority = 2,
                                 .task_tcb = &task_manager_mem.task_manager_task_tcb,
                                 .watchdog_timeout_ms = 10000,
                                 .last_checkin_time_ticks = 0xDEADBEEF,
                                 .has_registered = false,
                                 .task_type = OS};
pvdx_task_t magnetometer_task = {.name = "Magnetometer",
                                 .enabled = false,
                                 .handle = NULL,
                                 .command_queue = NULL,
                                 .init = init_magnetometer,
                                 .function = main_magnetometer,
                                 .stack_size = MAGNETOMETER_TASK_STACK_SIZE,
                                 .stack_buffer = magnetometer_mem.magnetometer_task_stack,
                                 .pvParameters = NULL,
                                 .priority = 2,
                                 .task_tcb = &magnetometer_mem.magnetometer_task_tcb,
                                 .watchdog_timeout_ms = 10000,
                                 .last_checkin_time_ticks = 0xDEADBEEF,
                                 .has_registered = false,
                                 .task_type = SENSOR};
pvdx_task_t shell_task = {.name = "Shell",
                          .enabled = false,
                          .handle = NULL,
                          .command_queue = NULL,
                          .init = NULL,
                          .function = main_shell,
                          .stack_size = SHELL_TASK_STACK_SIZE,
                          .stack_buffer = shell_mem.shell_task_stack,
                          .pvParameters = NULL,
                          .priority = 2,
                          .task_tcb = &shell_mem.shell_task_tcb,
                          .watchdog_timeout_ms = 10000,
                          .last_checkin_time_ticks = 0xDEADBEEF,
                          .has_registered = false,
                          .task_type = TESTING};
pvdx_task_t display_task = {.name = "Display",
                            .enabled = false,
                            .handle = NULL,
                            .command_queue = NULL,
                            .init = init_display,
                            .function = main_display,
                            .stack_size = DISPLAY_TASK_STACK_SIZE,
                            .stack_buffer = display_mem.display_task_stack,
                            .pvParameters = NULL,
                            .priority = 2,
                            .task_tcb = &display_mem.display_task_tcb,
                            .watchdog_timeout_ms = 10000,
                            .last_checkin_time_ticks = 0xDEADBEEF,
                            .has_registered = false,
                            .task_type = ACTUATOR};
pvdx_task_t heartbeat_task = {.name = "Heartbeat",
                              .enabled = true,
                              .handle = NULL,
                              .command_queue = NULL,
                              .init = NULL,
                              .function = main_heartbeat,
                              .stack_size = HEARTBEAT_TASK_STACK_SIZE,
                              .stack_buffer = heartbeat_mem.heartbeat_task_stack,
                              .pvParameters = NULL,
                              .priority = 2,
                              .task_tcb = &heartbeat_mem.heartbeat_task_tcb,
                              .watchdog_timeout_ms = 10000,
                              .last_checkin_time_ticks = 0xDEADBEEF,
                              .has_registered = false,
                              .task_type = ACTUATOR};
pvdx_task_t test_one_task = {.name = "Test 1",
                             .enabled = true,
                             .handle = NULL,
                             .command_queue = NULL,
                             .init = init_test_one,
                             .function = main_test_one,
                             .stack_size = TEST_ONE_TASK_STACK_SIZE,
                             .stack_buffer = test_one_mem.test_one_task_stack,
                             .pvParameters = NULL,
                             .priority = 2,
                             .task_tcb = &test_one_mem.test_one_task_tcb,
                             .watchdog_timeout_ms = 10000,
                             .last_checkin_time_ticks = 0xDEADBEEF,
                             .has_registered = false,
                             .task_type = TESTING};
pvdx_task_t test_two_task = {.name = "Test 2",
                             .enabled = true,
                             .handle = NULL,
                             .command_queue = NULL,
                             .init = init_test_two,
                             .function = main_test_two,
                             .stack_size = TEST_ONE_TASK_STACK_SIZE,
                             .stack_buffer = test_two_mem.test_two_task_stack,
                             .pvParameters = NULL,
                             .priority = 2,
                             .task_tcb = &test_two_mem.test_two_task_tcb,
                             .watchdog_timeout_ms = 10000,
                             .last_checkin_time_ticks = 0xDEADBEEF,
                             .has_registered = false,
                             .task_type = TESTING};

// and define their constant pointers
pvdx_task_t *const p_watchdog_task = &watchdog_task;
pvdx_task_t *const p_command_dispatcher_task = &command_dispatcher_task;
pvdx_task_t *const p_task_manager_task = &task_manager_task;
pvdx_task_t *const p_magnetometer_task = &magnetometer_task;
pvdx_task_t *const p_shell_task = &shell_task;
pvdx_task_t *const p_display_task = &display_task;
pvdx_task_t *const p_heartbeat_task = &heartbeat_task;
pvdx_task_t *const p_test_one_task = &test_one_task;
pvdx_task_t *const p_test_two_task = &test_two_task;
pvdx_task_t *const task_list_null_terminator = NULL;

// Global list of all tasks running on PVDXos (see `pvdx_task_t` definition in globals.h)
// ***********************************************************************
// ***  DO NOT CHANGE THE ORDER OF THE FIRST THREE OS-INTEGRITY TASKS  ***
// ***********************************************************************
// NOTE: Watchdog task must be first in the list, Command Dispatcher second, and Task Manager third.
// If you change the order of any of these, make sure that main.c reflects the change and update this comment.
pvdx_task_t *task_list[] = {
    p_watchdog_task, p_command_dispatcher_task, p_task_manager_task,       p_magnetometer_task, p_shell_task,
    p_display_task,  p_heartbeat_task,          task_list_null_terminator,
};

/**
 * \name pvdx_task_t *get_current_task(void)
 *
 * \return a pvdx_task_t *, a pointer to the current task struct.
 *
 * \warning TODO: is this thread-safe?
 */
inline pvdx_task_t *get_current_task(void) {
    // handle = NULL means current task
    return (pvdx_task_t *)pvTaskGetThreadLocalStoragePointer(NULL, 0);
}

// Given a pointer to a `pvdx_task_t` struct, returns the maximum block time in ticks when attempting to dequeue
// a command from the task's command queue.
inline TickType_t get_command_queue_block_time_ticks(pvdx_task_t *const task) {
    return pdMS_TO_TICKS(task->watchdog_timeout_ms / 2);
}

inline bool should_checkin(pvdx_task_t *const p_task) {
    return p_task->enabled;
}
