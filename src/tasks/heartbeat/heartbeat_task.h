#ifndef HEARTBEAT_TASK_H_
#define HEARTBEAT_TASK_H_

#include <atmel_start.h>
#include <driver_init.h>

#include "rtos_start.h"

//Memory for the heartbeat task
#define HEARTBEAT_TASK_STACK_SIZE 128 //Size of the stack in words (multiply by 4 to get bytes)
extern StackType_t heartbeatTaskStack[HEARTBEAT_TASK_STACK_SIZE];
extern StaticTask_t heartbeatTaskTCB;

void heartbeat_main(void *pvParameters);

#endif // HEARTBEAT_TASK_H_