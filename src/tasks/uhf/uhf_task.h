#ifndef UHF_TASK_H
#define UHF_TASK_H

#include "globals.h"
#include "logging.h"
#include "rtos_start.h"
#include "stream_buffer.h"

#include <atmel_start.h>
#include <driver_init.h>

// ### Config Variables ###
#define UHF_INCOMING_MESSAGES_STREAM_LENGTH 512  // Bytes
#define UHF_OUTGOING_MESSAGES_STREAM_LENGTH 1024 // Bytes

#define UHF_POLLING_DELAY_MS 500 // How often the UHF loop runs
// ########################

// Memory for the uhf task
#define UHF_TASK_STACK_SIZE 512 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
struct uhfTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t uhfTaskStack[UHF_TASK_STACK_SIZE];
    StaticTask_t uhfTaskTCB;
};

extern struct uhfTaskMemory uhfMem;

void uhf_main(void *pvParameters);

// Send a message through the UHF radio by calling these functions
status_t uhf_send_message_blocking(char *message, size_t length);
status_t uhf_send_message_nonblocking(char *message, size_t length);

#endif // UHF_TASK_H