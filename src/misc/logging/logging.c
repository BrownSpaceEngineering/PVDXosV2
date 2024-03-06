#include "logging.h"
#include <stdlib.h>
#include <stdarg.h>
#include "SEGGER_RTT.h"
void fatal(const char* string,...){
    va_list args;
    va_start(args, string);
    unsigned int BufferIndex= 0;
    SEGGER_RTT_vprintf(BufferIndex,string, &args); // Use vprintf to print with variable arguments
    
    va_end(args);
}
void warning(const char* string,...){
    va_list args;
    va_start(args, string);
    unsigned int BufferIndex= 0;
    SEGGER_RTT_vprintf(BufferIndex,string, &args); // Use vprintf to print with variable arguments
    
    va_end(args);
}
void info(char* string,...){
    va_list args;
    va_start(args, string);
    unsigned int BufferIndex= 0;
    SEGGER_RTT_vprintf(BufferIndex,string, &args); // Use vprintf to print with variable arguments
    va_end(args);
}

void debug(const char* string,...){
    return;
    va_list args;
    va_start(args, string);
    unsigned int BufferIndex= 0;
    SEGGER_RTT_vprintf(BufferIndex,string, &args); // Use vprintf to print with variable arguments
    va_end(args);
}