#include "heartbeat_task.h"
#include "logging.h"

struct heartbeatTaskMemory heartbeatMem;

void heartbeat_main(void *pvParameters) {
    info("Heartbeat Task Started!\r\n");
    //NOTE: false is on for some reason on the orange LEDs

    //In release build, make sure orange LEDs are off
    #if defined(RELEASE)
    gpio_set_pin_level(LED_Orange1, true);
    gpio_set_pin_level(LED_Orange2, true);
    #endif

    while(1) {
        // Print the current time
        uint32_t current_time = xTaskGetTickCount();
        debug("heartbeat: Current time is %d\n", current_time);

        //Devbuild heartbeat pattern (Smoothly turning on and off LEDs in a line)
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

        //Testing heartbeat pattern (Blinking LEDs in an oscillating pattern [ON/OFF/ON] -> [OFF/ON/OFF])
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

        //Release heartbeat pattern (Red LED only, blinking at 1Hz)
        #if defined(RELEASE)
        gpio_set_pin_level(LED_Red, true);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_pin_level(LED_Red, false);
        vTaskDelay(pdMS_TO_TICKS(500));
        #endif

        watchdog_checkin(); // Check in with the watchdog
    }
}