#include "task_list.h"

// Global list of all tasks running on PVDXos (see `pvdx_task_t` definition in globals.h)
// ***********************************************************************
// ***  DO NOT CHANGE THE ORDER OF THE FIRST THREE OS-INTEGRITY TASKS  ***
// ***********************************************************************
// NOTE: Watchdog task must be first in the list, Command Dispatcher second, and Task Manager third.
// If you change the order of any of these, make sure that main.c reflects the change and update this comment.
pvdx_task_t task_list[] = {
    {
        "Watchdog", true, NULL, main_watchdog, WATCHDOG_TASK_STACK_SIZE, watchdog_mem.watchdog_task_stack, NULL, 3, &watchdog_mem.watchdog_task_tcb, 10000, 0xDEADBEEF, false
    },
    {
        "CommandDispatcher", true, NULL, main_command_dispatcher, COMMAND_DISPATCHER_TASK_STACK_SIZE, command_dispatcher_mem.command_dispatcher_task_stack, NULL, 2, &command_dispatcher_mem.command_dispatcher_task_tcb, 10000, 0xDEADBEEF, false
    },
    {
        "TaskManager", true, NULL, main_task_manager, TASK_MANAGER_TASK_STACK_SIZE, task_manager_mem.task_manager_task_stack, NULL, 2, &task_manager_mem.task_manager_task_tcb, 10000, 0xDEADBEEF, false
    },
    // {
    //     "Shell", true, NULL, main_shell, SHELL_TASK_STACK_SIZE, shell_mem.shell_task_stack, NULL, 2, &shell_mem.shell_task_tcb, 10000, 0xDEADBEEF, false
    // },
    // {
    //     "Display", true, NULL, main_display, DISPLAY_TASK_STACK_SIZE, display_mem.display_task_stack, NULL, 2, &display_mem.display_task_tcb, 10000, 0xDEADBEEF, false
    // },
    // {
    //     "Heartbeat", true, NULL, main_heartbeat, HEARTBEAT_TASK_STACK_SIZE, heartbeat_mem.heartbeat_task_stack, NULL, 1, &heartbeat_mem.heartbeat_task_tcb, 10000, 0xDEADBEEF, false
    // },

    // Null terminator for the array (since size is unspecified)
    NULL_TASK
};

// Returns the pvdx_task_t struct associated with a FreeRTOS task handle
// WARNING: This function is not thread-safe and should only be called from within a critical section
pvdx_task_t* get_task(TaskHandle_t handle) {
    pvdx_task_t* p_task = task_list;

    while (p_task->name != NULL) {
        if (p_task->handle == handle) {
            return p_task;
        }
        p_task++;
    }
    
    return p_task;
}