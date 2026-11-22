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

// Lifecycle hook: enabling the task authorizes exactly one scan.
void reflash_prepare_resume(void);
// Called only by main_reflash_task; scans are serialized in that task.
status_t reflash_bootloaders(void);
void reflash_report_completion(void);
void reflash_checkin(void);

void main_reflash_task(void *pvParameters);

#endif
