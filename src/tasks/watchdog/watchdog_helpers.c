/**
 * watchdog_helpers.c
 *
 * Helper functions for the watchdog task. This task is responsible for monitoring the check-ins of other tasks
 * and resetting the system if a task fails to check in within the allowed time.
 *
 * Created: January 28, 2024
 * Authors: Oren Kohavi, Tanish Makadia, Siddharta Laloux
 */

#include "watchdog_task.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

// Updates the last checkin time of the given task to prove that it is still running
void watchdog_checkin(pvdx_task_t *const p_task) {
    if (!p_task) {
        fatal("Attempted to update checkin time of null task!");
    }

    if (!(p_task->has_registered)) {
        // something went wrong because a task that is checking in should have 'has_registered' set to true
        fatal("watchdog: %s task tried to check in without registering\n", p_task->name);
    }

    lock_mutex(task_list_mutex);

    // update the last checkin time
    p_task->last_checkin_time_ticks = xTaskGetTickCount();

    unlock_mutex(task_list_mutex);
    debug("watchdog: %s task checked in\n", p_task->name);
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

QueueHandle_t init_watchdog(void) {
    // Choose the period of the hardware watchdog timer
    uint8_t watchdog_period = WDT_CONFIG_PER_CYC16384;

    // Disable the watchdog before configuring
    watchdog_disable(p_watchdog);

    // Configure the watchdog
    uint8_t watchdog_earlywarning_period =
        watchdog_period - 1; // Each increment of 1 doubles the period (see ASF/samd51a/include/component/wdt.h)
    watchdog_set_early_warning_offset(p_watchdog,
                                      watchdog_earlywarning_period); // Early warning will trigger halfway through the watchdog period
    watchdog_enable_early_warning(p_watchdog);                       // Enable early warning interrupt
    watchdog_set_period(p_watchdog, watchdog_period);                // Set the watchdog period
    watchdog_wait_for_register_sync(p_watchdog, WDT_SYNCBUSY_ENABLE | WDT_SYNCBUSY_WEN); // Wait for register synchronization

    // Enable the watchdog
    watchdog_enable(p_watchdog);

    // Configure the watchdog early warning interrupt
    NVIC_SetPriority(WDT_IRQn, 3);                      // Set the interrupt priority
    NVIC_EnableIRQ(WDT_IRQn);                           // Enable the WDT_IRQn interrupt
    NVIC_SetVector(WDT_IRQn, (uint32_t)(&WDT_Handler)); // When the WDT_IRQn interrupt is triggered, call the WDT_Handler function

    info("Hardware Watchdog Initialized\n");

    // Create watchdog command queue
    watchdog_command_queue_handle = xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE,
                                                       watchdog_mem.watchdog_command_queue_buffer, &watchdog_mem.watchdog_task_queue);

    if (watchdog_command_queue_handle == NULL) {
        fatal("Failed to create watchdog queue!\n");
    }

    // Initialize the 'last_checkin_time_ticks' field of each task
    // Iterate using the 'name' field rather than the handle field, since not all tasks will have a handle at this point
    for (size_t i = 0; task_list[i] != NULL; i++) {
        task_list[i]->last_checkin_time_ticks =
            0; // 0 Is a special value that indicates that the task has not checked in yet (or is not running)
    }

    for (size_t i = 0; task_list[i] != NULL; i++) {
        task_list[i]->has_registered = false;
    }

    return watchdog_command_queue_handle;
}

/* Temporarily commented out (so that specific_handlers.c works)
void WDT_Handler(void) {
    debug("watchdog: WDT_Handler executed\n");

    // Check if the early warning interrupt is triggered
    if (watchdog_get_early_warning_bit(p_watchdog)) {
        debug("watchdog: Early warning interrupt detected\n");
        watchdog_clear_early_warning_bit(p_watchdog); // Clear the early warning interrupt flag
        watchdog_early_warning_callback(); // Call the early warning callback function
    }
}
*/

void early_warning_callback_watchdog(void) {
    warning("Early warning callback executed\n");
    // This function gets called when the watchdog is almost out of time
    // TODO: Test if this works
    // This is also fine to leave blank
    gpio_set_pin_level(LED_Orange1, false);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_pin_level(LED_Orange2, false);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_pin_level(LED_RED, true);
    vTaskDelay(pdMS_TO_TICKS(400));
    gpio_set_pin_level(LED_Orange1, true);
    vTaskDelay(pdMS_TO_TICKS(33));
    gpio_set_pin_level(LED_Orange2, true);
    vTaskDelay(pdMS_TO_TICKS(33));
    gpio_set_pin_level(LED_RED, false);
    vTaskDelay(pdMS_TO_TICKS(300));
    ;
}

// Pets the watchdog to prevent it from resetting the system by setting the clear key correctly
void pet_watchdog(void) {
    debug("hardware-watchdog: Petted\n");
    watchdog_feed(p_watchdog);
}

// Kicks the watchdog to reset the system by setting the clear key incorrectly
void kick_watchdog(void) {
    debug("hardware-watchdog: Kicked\n");
    watchdog_set_clear_register(p_watchdog, 0x12); // set intentionally wrong clear key, so the watchdog will reset the system
    // this function should never return because the system should reset
}

// Given a pointer to a `pvdx_task_t` struct, returns a command to check-in with the watchdog task.
inline command_t get_watchdog_checkin_command(pvdx_task_t *const task) {
    // NOTE: Be sure to use the address of the task handle within the global task list (static lifetime) to ensure
    // that `*p_data` is still valid when the command is received.
    return (command_t){.target = p_watchdog_task,
                       .operation = OPERATION_CHECKIN,
                       .p_data = task,
                       .len = sizeof(pvdx_task_t *),
                       .result = NO_STATUS_RETURN,
                       .callback = NULL};
}

// Registers a task with the watchdog so that checkins are monitored.
// WARNING: This function is not thread-safe and should only be called from within a critical section
void register_task_with_watchdog(pvdx_task_t *const p_task) {
    // TODO: check if we can assert task_list_mutex acquired in current task
    if (p_task == NULL) {
        fatal("Tried to register a NULL task handle with watchdog\n");
    }

    if (p_task->has_registered) {
        fatal("%s task tried to register a second time with watchdog\n", p_task->name);
    }

    // initialize running times and require the task to check in
    p_task->last_checkin_time_ticks = xTaskGetTickCount();
    p_task->has_registered = true;
    debug("%s task registered with watchdog\n", p_task->name);
}

// Unregisters a task with the watchdog so that checkins are no longer monitored.
// WARNING: This function is not thread-safe and should only be called from within a critical section
void unregister_task_with_watchdog(pvdx_task_t *const p_task) {
    if (!p_task) {
        fatal("Attempted to update checkin time of null task!");
    }

    if (!(p_task->has_registered)) {
        fatal("%s task tried to unregister a second time with watchdog\n", p_task->name);
    }

    p_task->last_checkin_time_ticks = 0xDEADBEEF; // 0xDEADBEEF is a special value that indicates that the task is not running
    p_task->has_registered = false;
    debug("%s task unregistered with watchdog\n", p_task->name);
}

// Executes a command received by the watchdog task
void exec_command_watchdog(command_t *const p_cmd) {
    if (p_cmd->target != p_watchdog_task) {
        fatal("watchdog: command target is not watchdog! target: %d operation: %d\n", p_cmd->target->name, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        case OPERATION_CHECKIN:
            watchdog_checkin((pvdx_task_t *)p_cmd->p_data);
            break;
        default:
            fatal("watchdog: Invalid operation! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
            break;
    }
}
