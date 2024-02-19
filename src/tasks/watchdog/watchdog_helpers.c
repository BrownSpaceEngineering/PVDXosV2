#include "watchdog_task.h"

void watchdog_init(uint8_t watchdog_period, bool always_on) {
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
    wdt_disable(watchdog_descriptor_p);

    // Configure the watchdog
    uint8_t watchdog_earlywarning_period = watchdog_period - 1; // Early warning will trigger halfway through the watchdog period
    hri_wdt_set_EWCTRL_EWOFFSET_bf(watchdog_p, watchdog_earlywarning_period); // Early warning will trigger halfway through the watchdog period
    hri_wdt_set_INTEN_EW_bit(watchdog_p); // Enable early warning interrupt
    hri_wdt_write_CONFIG_PER_bf(watchdog_p, watchdog_period); // Set the watchdog period
    hri_wdt_wait_for_sync(watchdog_p, WDT_SYNCBUSY_ENABLE | WDT_SYNCBUSY_WEN); // Wait for register synchronization

    // Enable the watchdog
    wdt_enable(watchdog_descriptor_p);

    watchdog_enabled = true;
    NVIC_SetPriority(WDT_IRQn, 3); // Set the interrupt priority
    NVIC_EnableIRQ(WDT_IRQn); // Enable the WDT_IRQn interrupt
    NVIC_SetVector(WDT_IRQn, (uint32_t)(&WDT_Handler)); // When the WDT_IRQn interrupt is triggered, call the WDT_Handler function

    printf("Watchdog initialized\n");
}

void WDT_Handler(void) {
    printf("Executing WDT_Handler\n");

    // Check if the early warning interrupt is triggered
    if (hri_wdt_get_interrupt_EW_bit(watchdog_p)) {
        printf("Detected early warning interrupt\n");
        hri_wdt_clear_interrupt_EW_bit(watchdog_p); // Clear the early warning interrupt flag
        watchdog_early_warning_callback(); // Call the early warning callback function
    }
}

void watchdog_early_warning_callback(void) {
    printf("Executing watchdog_early_warning_callback\n");
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

void watchdog_pet(void) {
    printf("Petting the watchdog\n");
    wdt_feed(watchdog_descriptor_p);
}

void watchdog_kick(void) {
    printf("Kicking the watchdog\n");
    hri_wdt_write_CLEAR_reg(watchdog_p, 0x12); // set intentionally wrong clear key, so the watchdog will reset the system
    // this function should never return because the system should reset
}

// If I am a battery task, I call this function: `watchdog_checkin(BATTERY_TASK)`
int watchdog_checkin(task_type_t task_index) {
    // sanity checks
    if (task_index < 0 || task_index >= NUM_TASKS) {
        return -1;
    }

    if (!should_checkin[task_index]) {
        return -1;
    }

    // add the current time to the running times array
    running_times[task_index] = xTaskGetTickCount();
    return 0;
}

int watchdog_register_task(task_type_t task_index) {
    // sanity checks
    if (task_index < 0 || task_index >= NUM_TASKS) {
        return -1;
    }

    if (should_checkin[task_index]) {
        // something went wrong because we define unregistered tasks to have 'should_checkin' set to false
        watchdog_kick();
    }

    // initialize running times and require the task to check in
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