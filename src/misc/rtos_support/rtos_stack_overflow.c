#include "FreeRTOS.h"
#include "task.h"
#include "logging.h"

void vApplicationStackOverflowHook(TaskHandle_t xTask, signed char *pcTaskName){
    //This function is called when a stack overflow is detected
    warning("\r\n --- STACK OVERFLOW DETECTED: Task '%s' --- \r\n", pcTaskName);
    warning("TODO: The watchdog timer should be kicked, and the system should be reset.\r\n");
}