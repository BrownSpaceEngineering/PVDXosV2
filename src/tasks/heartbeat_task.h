#ifndef HEARTBEAT_TASK_H_
#define HEARTBEAT_TASK_H_

#include <atmel_start.h>
#include <driver_init.h>

#include "rtos_start.h"

void heartbeat_main(void *pvParameters);

#endif // HEARTBEAT_TASK_H_