#include "watchdog_task.h"

// If the difference between the current time and the time in the running_times array is greater than the allowed time,
// then the task has not checked in and the watchdog should reset the system. Refer to "globals.h" to see the order in which
// tasks are registered.
static uint32_t allowed_times[NUM_TASKS] = {1000, 1000};

void watchdog_init(int watchdog_period, bool always_on) {
    // Initialize the running times
    for (int i = 0; i < NUM_TASKS; i++) {
        running_times[i] = 0; // 0 Is a special value that indicates that the task has not checked in yet (or is not running)
    }

    for (int i = 0; i < NUM_TASKS; i++) {
        should_checkin[i] = false;
    }

    // Make sure that critical tasks are always supposed to check in
    should_checkin[WATCHDOG_TASK] = true;

    // Disable the watchdog before configuring
    WDT->CTRLA.reg &= ~(WDT_CTRLA_ENABLE | WDT_CTRLA_WEN);
    while (WDT->SYNCBUSY.bit.ENABLE);

    // Configure the watchdog
    uint8_t watchdog_earlywarning_period = watchdog_period - 1;
    WDT->EWCTRL.bit.EWOFFSET = watchdog_earlywarning_period; // Early warning will trigger halfway through the watchdog period
    WDT->CONFIG.reg = watchdog_period | watchdog_earlywarning_period; // Set the window value (e.g., no windowing)    
    WDT->INTENSET.bit.EW = 1; // Enable early warning interrupt
    while (WDT->SYNCBUSY.bit.ENABLE);

    // Enable the watchdog
    if (always_on) {
        WDT->CTRLA.reg |= WDT_CTRLA_ENABLE;
    } else {
        WDT->CTRLA.reg |= WDT_CTRLA_ENABLE | WDT_CTRLA_ALWAYSON;
    }

    while (WDT->SYNCBUSY.bit.ENABLE);

    watchdog_enabled = true;
    NVIC_SetPriority(WDT_IRQn, 3); // Set the interrupt priority
    NVIC_EnableIRQ(WDT_IRQn); // Enable the WDT_IRQn interrupt
    NVIC_SetVector(WDT_IRQn, (uint32_t)(&WDT_Handler)); // When the WDT_IRQn interrupt is triggered, call the WDT_Handler function
}

void WDT_Handler() {
    // Check if the early warning interrupt is triggered
    if (WDT->INTFLAG.bit.EW) {
        // Clear the early warning interrupt flag
        WDT->INTFLAG.reg = 1;
        // Call the early warning callback function
        watchdog_early_warning_callback();
    }
}

void watchdog_early_warning_callback() {
    // This function gets called when the watchdog is almost out of time
    // TODO Test if this works
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

void watchdog_pet() {
    watchdog->CLEAR.reg = WDT_CLEAR_CLEAR_KEY;

    // Wait for synchronization
    while (watchdog->SYNCBUSY.bit.ENABLE);
}

void watchdog_kick() {
    watchdog->CLEAR.reg = 0x12; // set intentionally wrong clear key, so the watchdog will reset the system
    // this function should never return because the system should reset
}

// If I am a battery task, I call this function: `watchdog_check_in(BATTERY_TASK)`
int watchdog_checkin(task_type_t task_index) {
    if (task_index < 0 || task_index >= NUM_TASKS) {
        return -1;
    }

    if (!should_checkin[task_index]) {
        return -1;
    }

    running_times[task_index] = xTaskGetTickCount();
    return 0;
}

int watchdog_register_task(task_type_t task_index) {
    if (task_index < 0 || task_index >= NUM_TASKS) {
        return -1;
    }

    if (should_checkin[task_index]) {
        // something went wrong because we define unregistered tasks to have 'should_checkin' set to false
        watchdog_kick();
    }

    running_times[task_index] = xTaskGetTickCount();
    should_checkin[task_index] = true;
    return 0;
}

int watchdog_unregister_task(task_type_t task_index) {
    if (task_index < 0 || task_index >= NUM_TASKS) {
        return -1;
    }

    if (!should_checkin[task_index]) {
        // something went wrong because we define unregistered tasks to have 'should_checkin' set to false
        // and an unregistered task should not be able to unregister itself again
        watchdog_kick();
    }

    running_times[task_index] = 0xDEADBEEF; // 0xDEADBEEF is a special value that indicates that the task is not running
    should_checkin[task_index] = false;
    return 0;
}
