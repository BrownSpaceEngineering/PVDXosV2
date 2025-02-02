/**
 * heartbeat_main.c
 *
 * Main loop of the Heartbeat task, which simply blinks an LED at a fixed rate to indicate that the
 * system is still running.
 *
 * Created: November 20, 2023
 * Authors: Oren Kohavi, Tanish Makadia
 */

#include "heartbeat_task.h"
#include "logging.h"

heartbeat_task_memory_t heartbeat_mem;

void main_heartbeat(void *pvParameters) {
    info("heartbeat: Task Started!\n");

    // Cache the watchdog checkin command to avoid creating it every iteration
    const command_t cmd_checkin = get_watchdog_checkin_command();

    // NOTE: false is on for some reason on the orange LEDs
    // In release build, make sure orange LEDs are off
    #if defined(RELEASE)
        gpio_set_pin_level(LED_Orange1, true);
        gpio_set_pin_level(LED_Orange2, true);
    #endif

    while (true) {
        debug_impl("\n---------- Heartbeat Task Loop ----------\n");

        // Print the current time
        uint32_t current_time = xTaskGetTickCount();
        debug("heartbeat: Current time is %d\n", current_time);

        // Devbuild heartbeat pattern (Smoothly turning on and off LEDs in a line)
        #if defined(DEVBUILD)
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
            vTaskDelay(pdMS_TO_TICKS(300));
        #endif

        // Testing heartbeat pattern (Blinking LEDs in an oscillating pattern [ON/OFF/ON] -> [OFF/ON/OFF])
        #if defined(UNITTEST)
            gpio_set_pin_level(LED_Orange1, false);
            gpio_set_pin_level(LED_Orange2, true);
            gpio_set_pin_level(LED_Red, true);
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_set_pin_level(LED_Orange1, true);
            gpio_set_pin_level(LED_Orange2, false);
            gpio_set_pin_level(LED_Red, false);
            vTaskDelay(pdMS_TO_TICKS(500));
        #endif

        // Release heartbeat pattern (Red LED only, blinking at 1Hz)
        #if defined(RELEASE)
            gpio_set_pin_level(LED_Red, true);
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_set_pin_level(LED_Red, false);
            vTaskDelay(pdMS_TO_TICKS(500));
        #endif

        // Check in with the watchdog task
        enqueue_command(&cmd_checkin);
        debug("heartbeat: Enqueued watchdog checkin command\n");
    }
}
