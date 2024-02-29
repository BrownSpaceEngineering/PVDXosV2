#include "watchdog_task.h"

// If the difference between the current time and the time in the running_times array is greater than the allowed time,
// then the task has not checked in and the watchdog should reset the system. Refer to "globals.h" to see the order in which
// tasks are registered.

volatile Wdt *const p_watchdog = WDT;
uint32_t running_times[NUM_TASKS];
bool should_checkin[NUM_TASKS];
bool watchdog_enabled = false;

void watchdog_init(uint8_t watchdog_period, bool always_on) {
    // Initialize the running times
    for (int i = 0; i < NUM_TASKS; i++) {
        running_times[i] = 0; // 0 Is a special value that indicates that the task has not checked in yet (or is not running)
    }

    for (int i = 0; i < NUM_TASKS; i++) {
        should_checkin[i] = false;
    }

    // Disable the watchdog before configuring
    watchdog_disable(p_watchdog);

    // Configure the watchdog (casting to (Wdt *) bypasses volatile qualifier warning)
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

    printf("watchdog: Initialized\n");
}

void WDT_Handler(void) {
    printf("watchdog: WDT_Handler executed\n");

    // Check if the early warning interrupt is triggered
    if (watchdog_get_early_warning_bit(p_watchdog)) {
        printf("watchdog: Early warning interrupt detected\n");
        watchdog_clear_early_warning_bit(p_watchdog); // Clear the early warning interrupt flag
        watchdog_early_warning_callback(); // Call the early warning callback function
    }
}

void watchdog_early_warning_callback(void) {
    printf("watchdog: Early warning callback executed\n");
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

void watchdog_pet(void) {
    printf("watchdog: Petted\n");
    watchdog_feed(p_watchdog);
}

void watchdog_kick(void) {
    printf("watchdog: Kicked\n");
    watchdog_set_clear_register(p_watchdog, 0x12); // set intentionally wrong clear key, so the watchdog will reset the system
    // this function should never return because the system should reset
}

// If I am a battery task, I call this function: `watchdog_checkin(BATTERY_TASK)`
int watchdog_checkin(task_type_t task_index) {
    // sanity checks
    if (task_index >= NUM_TASKS) {
        return -1;
    }

    if (!should_checkin[task_index]) {
        return -1;
    }

    // add the current time to the running times array
    running_times[task_index] = xTaskGetTickCount();
    printf("watchdog: Task %d checked in\n", task_index);
    return 0;
}

int watchdog_register_task(task_type_t task_index) {
    // sanity checks
    if (task_index >= NUM_TASKS) {
        return -1;
    }

    if (should_checkin[task_index]) {
        // something went wrong because we define unregistered tasks to have 'should_checkin' set to false
        watchdog_kick();
    }

    // initialize running times and require the task to check in
    running_times[task_index] = xTaskGetTickCount();
    should_checkin[task_index] = true;
    printf("watchdog: Task %d registered\n", task_index);
    return 0;
}

int watchdog_unregister_task(task_type_t task_index) {
    if (task_index >= NUM_TASKS) {
        return -1;
    }

    if (!should_checkin[task_index]) {
        // something went wrong because we define unregistered tasks to have 'should_checkin' set to false
        // and an unregistered task should not be able to unregister itself again
        watchdog_kick();
    }

    running_times[task_index] = 0xDEADBEEF; // 0xDEADBEEF is a special value that indicates that the task is not running
    should_checkin[task_index] = false;
    printf("watchdog: Task %d unregistered\n", task_index);
    return 0;
}
