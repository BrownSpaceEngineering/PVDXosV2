#ifndef LOGIN_H
#define LOGIN_H


void fatal(const char* string, ...);//worst case restart
void warning(const char* string, ...);//second 
void info(char* string,...);//something happened
void debug(const char* string,...);// something small 
#endif