#ifndef OS_DELAY_H
#define OS_DELAY_H

#include <stdint.h>

// Delays that work both before and after the FreeRTOS scheduler starts, and always last at least as long as asked.
// Do not call them with the scheduler suspended (vTaskSuspendAll()): they can neither block nor busy-wait safely there.
void os_delay_ms(uint32_t ms);
void os_delay_us(uint32_t us);

#endif // OS_DELAY_H
