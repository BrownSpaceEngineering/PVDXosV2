#include "FreeRTOS.h"
#include "task.h"
#include "SEGGER_RTT_printf.h"

void vApplicationStackOverflowHook(TaskHandle_t xTask, signed char *pcTaskName){
    //This function is called when a stack overflow is detected
    printf("\r\n --- STACK OVERFLOW DETECTED: Task '%s' --- \n", pcTaskName);
    printf("TODO: The watchdog timer should be kicked, and the system should be reset.\n");
}