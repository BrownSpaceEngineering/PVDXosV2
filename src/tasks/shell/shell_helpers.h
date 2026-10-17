#ifndef SHELL_HELPERS_H
#define SHELL_HELPERS_H

#include "SEGGER_RTT.h"
#include "shell_task.h"

size_t get_line_from_terminal(uint8_t *p_linebuffer);
void clear_RTT_input_buffer();
void terminal_printf(const char *string, ...);

char *strtok(char *str, const char *delim);
int strcmp(const char *str1, const char *str2);

#endif // SHELL_HELPERS_H