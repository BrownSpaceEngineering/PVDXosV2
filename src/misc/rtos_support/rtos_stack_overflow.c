#include "FreeRTOS.h"
#include "task.h"
#include "logging.h"

void vApplicationStackOverflowHook(TaskHandle_t xTask, signed char *pcTaskName){
    //This function is called when a stack overflow is detected
    warning("\n --- STACK OVERFLOW DETECTED: Task '%s' --- \n", pcTaskName);
    fatal("System restarting due to a suspected stack overflow in task '%s'\n", pcTaskName);
}