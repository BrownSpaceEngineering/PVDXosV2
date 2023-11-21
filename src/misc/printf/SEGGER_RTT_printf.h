#ifndef SEGGER_RTT_PRINTF_H
#define SEGGER_RTT_PRINTF_H

#include "SEGGER_RTT.h"
#include "SEGGER_RTT_Conf.h"

#define printf(...) SEGGER_RTT_printf(0, __VA_ARGS__)

int SEGGER_RTT_printf(unsigned BufferIndex, const char * sFormat, ...);

#endif // SEGGER_RTT_PRINTF_H