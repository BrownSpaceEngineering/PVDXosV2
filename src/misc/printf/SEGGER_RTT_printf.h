#ifndef SEGGER_RTT_PRINTF_H
#define SEGGER_RTT_PRINTF_H

#include "SEGGER_RTT.h"
#include "SEGGER_RTT_Conf.h"

#define RTT_OUT_BUFFER_INDEX 0
#define printf(...) SEGGER_RTT_printf(RTT_OUT_BUFFER_INDEX, __VA_ARGS__)
// #define PRINTER(f_, ...) SEGGER_RTT_printf(0, f_, ##__VA_ARGS__)
// #define printf(...) PRINTER(__VA_ARGS__, "")
// #define printf(sf) __printf(sf)

void RTT_putchar(char c);
// int SEGGER_RTT_printf(unsigned BufferIndex, const char *sFormat, ...);

// int __printf(char *sFormat) { return SEGGER_RTT_printf(0, sFormat); }

#endif  // SEGGER_RTT_PRINTF_H