#ifndef SEGGER_RTT_PRINTF_H
#define SEGGER_RTT_PRINTF_H

#include "SEGGER_RTT.h"
#include "SEGGER_RTT_Conf.h"

#define printf(sFormat, ...) SEGGER_RTT_printf(0, sFormat, ##__VA_ARGS__)

void RTT_putchar(char c);
int SEGGER_RTT_printf(unsigned BufferIndex, const char *sFormat, ...);

#endif  // SEGGER_RTT_PRINTF_H