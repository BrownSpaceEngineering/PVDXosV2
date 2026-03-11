#ifndef REFLASH_H
#define REFLASH_H

#include "globals.h"

#define REFLASH_TASK_STACK_SIZE 1024

typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t reflash_task_stack[REFLASH_TASK_STACK_SIZE];
    StaticTask_t reflash_task_tcb;
} reflash_task_memory_t;

extern reflash_task_memory_t reflash_mem;

void reflash_bootloaders(void);
void main_reflash_task(void *pvParameters);

#endif