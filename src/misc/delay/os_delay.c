/**
 * os_delay.c
 *
 * Delays that work both before and after the FreeRTOS scheduler starts. Before it starts, there is no task to block,
 * so they busy-wait with the ASF delay_ms()/delay_us(). Once it has started, they block with vTaskDelay(): the ASF
 * delays reprogram SysTick, which FreeRTOS uses for its tick, and leave it at the wrong rate.
 *
 * Created: November 4, 2026
 * Authors: PVDX
 */

#include "os_delay.h"

#include <FreeRTOS.h>
#include <task.h>

#include "hal_delay.h"

// vTaskDelay(n) returns at the n-th tick boundary, which can be up to a tick less than n ticks away, so one extra tick
// keeps the delay from ever being shorter than asked
static void block_for_ticks(uint64_t ticks) {
    vTaskDelay((TickType_t)ticks + 1);
}

void os_delay_ms(uint32_t ms) {
    if (ms == 0) {
        return;
    }
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        block_for_ticks(((uint64_t)ms * configTICK_RATE_HZ + 999u) / 1000u);
        return;
    }
    // The ASF delays take a uint16_t
    for (; ms > UINT16_MAX; ms -= UINT16_MAX) {
        delay_ms(UINT16_MAX);
    }
    delay_ms((uint16_t)ms);
}

void os_delay_us(uint32_t us) {
    if (us == 0) {
        return;
    }
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        block_for_ticks(((uint64_t)us * configTICK_RATE_HZ + 999999u) / 1000000u);
        return;
    }
    for (; us > UINT16_MAX; us -= UINT16_MAX) {
        delay_us(UINT16_MAX);
    }
    delay_us((uint16_t)us);
}
