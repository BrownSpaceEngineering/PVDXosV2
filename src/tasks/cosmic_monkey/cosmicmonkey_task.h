#ifndef COSMICMONKEY_TASK_H_
#define COSMICMONKEY_TASK_H_

#include <atmel_start.h>
#include <driver_init.h>
#include "globals.h"
#include "rtos_start.h"

void perform_flip();
void cosmicmonkey_main(void *pvParameters);
#define COSMICMONKEY_TASK_STACK_SIZE 128

struct cosmicmonkeyTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t cosmicmonkeyTaskStack[COSMICMONKEY_TASK_STACK_SIZE];
    StaticTask_t cosmicmonkeyTaskTCB;
};
extern struct cosmicmonkeyTaskMemory cosmicmonkeyMem;

struct cosmicmonkeyTaskArguments {
    int frequency;
};
extern struct cosmicmonkeyTaskArguments cosmicmonkeyTaskArgs;


#endif