#ifndef GLOBAL_H_
#define GLOBAL_H_

//Atmel Includes
#include <atmel_start.h>
#include <driver_init.h>
#include <hal_adc_sync.h>

//Sensor Driver Includes
#include "sensor_drivers/rm3100.h"

//General Includes
#include "misc/printf/SEGGER_RTT_printf.h"
#include "tasks/heartbeat_task.h"

//m_sync descriptors
extern struct i2c_m_sync_desc RM3100;

#endif