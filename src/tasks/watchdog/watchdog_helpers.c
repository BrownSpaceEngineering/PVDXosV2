#include "watchdog_task.h"

// If the difference between the current time and the time in the running_times array is greater than the allowed time,
// then the task has not checked in and the watchdog should reset the system. Refer to "globals.h" to see the order in which
// tasks are registered

void init_watchdog(void) {
    // Choose the period of the hardware watchdog timer
    uint8_t watchdog_period = WDT_CONFIG_PER_CYC16384;

    // TODO: Create command queue
    watchdog_command_queue = xQueueCreateStatic(TASK_MANAGER_TASK_STACK_SIZE, COMMAND_QUEUE_ITEM_SIZE,
        task_manager_queue_buffer, &task_manager_mem.task_manager_task_queue);

    if (watchdog_command_queue == NULL) {
        fatal("watchdog: Failed to create watchdog queue!\n");
    }

    // Initialize the 'last_checkin' field of each task
    // Iterate using the 'name' field rather than the handle field, since not all tasks will have a handle at this point
    for (size_t i = 0; task_list[i].name != NULL; i++) {
        task_list[i].last_checkin = 0; // 0 Is a special value that indicates that the task has not checked in yet (or is not running)
    }

    for (size_t i = 0; task_list[i].name != NULL; i++) {
        task_list[i].has_registered = false;
    }

    // Disable the watchdog before configuring
    watchdog_disable(p_watchdog);

    // Configure the watchdog
    uint8_t watchdog_earlywarning_period = watchdog_period - 1; // Each increment of 1 doubles the period (see ASF/samd51a/include/component/wdt.h)
    watchdog_set_early_warning_offset(p_watchdog, watchdog_earlywarning_period); // Early warning will trigger halfway through the watchdog period
    watchdog_enable_early_warning(p_watchdog); // Enable early warning interrupt
    watchdog_set_period(p_watchdog, watchdog_period); // Set the watchdog period
    watchdog_wait_for_register_sync(p_watchdog, WDT_SYNCBUSY_ENABLE | WDT_SYNCBUSY_WEN); // Wait for register synchronization

    // Enable the watchdog
    watchdog_enable(p_watchdog);
    watchdog_enabled = true;

    // Configure the watchdog early warning interrupt
    NVIC_SetPriority(WDT_IRQn, 3); // Set the interrupt priority
    NVIC_EnableIRQ(WDT_IRQn); // Enable the WDT_IRQn interrupt
    NVIC_SetVector(WDT_IRQn, (uint32_t)(&WDT_Handler)); // When the WDT_IRQn interrupt is triggered, call the WDT_Handler function

    info("watchdog: Initialized\n");
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
    warning("watchdog: Early warning callback executed\n");
    // This function gets called when the watchdog is almost out of time
    // TODO: Test if this works
    // This is also fine to leave blank
    gpio_set_pin_level(LED_Orange1, false);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_pin_level(LED_Orange2, false);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_pin_level(LED_Red, true);
    vTaskDelay(pdMS_TO_TICKS(400));
    gpio_set_pin_level(LED_Orange1, true);
    vTaskDelay(pdMS_TO_TICKS(33));
    gpio_set_pin_level(LED_Orange2, true);
    vTaskDelay(pdMS_TO_TICKS(33));
    gpio_set_pin_level(LED_Red, false);
    vTaskDelay(pdMS_TO_TICKS(300));;
}

void pet_watchdog(void) {
    debug("watchdog: Petted\n");
    watchdog_feed(p_watchdog);
}

void kick_watchdog(void) {
    debug("watchdog: Kicked\n");
    watchdog_set_clear_register(p_watchdog, 0x12); // set intentionally wrong clear key, so the watchdog will reset the system
    // this function should never return because the system should reset
}

// TODO: This function should be sent through the command dispatcher as a
// high-priority command with xQueueSendToFront
// TODO: Should registration on init pass through the command dispatcher?
// TODO: Enable task needs to register and disable task needs to unregister
void watchdog_checkin(TaskHandle_t handle) {
    lock_mutex(task_list_mutex);
    
    pvdx_task_t *task = get_task(handle);

    if (!task->has_registered) {
        // something went wrong because a task that is checking in should have 'has_registered' set to true
        fatal("watchdog: %s task tried to check in without registering\n", task->name);
    }

    // update the last checkin time
    task->last_checkin = xTaskGetTickCount();

    unlock_mutex(task_list_mutex);

    debug("watchdog: %s task checked in\n", task->name);
}

// This function is a helper and does not get sent through the command dispatcher
void register_task_with_watchdog(TaskHandle_t handle) {
    lock_mutex(task_list_mutex);

    if (handle == NULL) {
        fatal("watchdog: Tried to register a NULL task handle\n");
    }

    pvdx_task_t *task = get_task(handle);

    if (task->handle != handle) {
        fatal("watchdog: Task Manager handle does not match current task handle!\n");
    }

    if (task->has_registered) {
        fatal("watchdog: %s task tried to register a second time\n", task->name);
    }

    // initialize running times and require the task to check in
    task->last_checkin = xTaskGetTickCount();
    task->has_registered = true;
    unlock_mutex(task_list_mutex);

    debug("watchdog: %s task registered\n", task->name);
}

// This function is a helper and does not get sent through the command dispatcher
void unregister_task_with_watchdog(TaskHandle_t handle) {
    lock_mutex(task_list_mutex);

    if (handle == NULL) {
        fatal("watchdog: Tried to unregister a NULL task handle\n");
    }
    
    pvdx_task_t *task = get_task(handle);

    if (task->handle != handle) {
        fatal("watchdog: Task Manager handle does not match current task handle!\n");
    }

    if (!task->has_registered) {
        fatal("watchdog: %s task tried to unregister a second time\n", task->name);
    }

    task->last_checkin = 0xDEADBEEF; // 0xDEADBEEF is a special value that indicates that the task is not running
    task->has_registered = false;

    unlock_mutex(task_list_mutex);
    debug("watchdog: %s task unregistered\n", task->name);
}

// TODO: set the payload of the command to the task handle
void exec_command_watchdog(command_t cmd) {
    switch (cmd.operation) {
        case OPERATION_CHECKIN:
            watchdog_checkin();
            break;
        default:
            fatal("watchdog: Invalid operation!\n");
            break;
    }
}