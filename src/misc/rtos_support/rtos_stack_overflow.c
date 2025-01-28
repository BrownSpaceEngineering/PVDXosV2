/**
 * rtos_stack_overflow.c
 * 
 * A handler for detected stack overflows in FreeRTOS tasks.
 * 
 * Created: February 15, 2024
 * Author: Oren Kohavi
 */

#include "FreeRTOS.h"
#include "logging.h"
#include "task.h"

void vApplicationStackOverflowHook(TaskHandle_t xTask, signed char *pcTaskName) {
    // This function is called when a stack overflow is detected
    warning("\n --- STACK OVERFLOW DETECTED: Task '%s' --- \n", pcTaskName);
    fatal("System restarting due to a suspected stack overflow in task '%s'\n", pcTaskName);
}