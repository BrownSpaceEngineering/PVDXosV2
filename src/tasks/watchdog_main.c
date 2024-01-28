#include "watchdog_task.h"

static volatile Wdt *watchdog = WDT;
static uint32_t running_times[NUM_TASKS];
static bool should_checkin[NUM_TASKS];
static bool watchdog_enabled = false;

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