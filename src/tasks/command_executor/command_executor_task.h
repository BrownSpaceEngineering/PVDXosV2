#ifndef COMMAND_EXECUTOR_H
#define COMMAND_EXECUTOR_H

#include "globals.h"

// An enum to represent the different operations that the command executor can perform
typedef enum {
    OPERATION_DISPLAY_SET_BUFFER = 0,
    OPERATION_DISPLAY_UPDATE,
    OPERATION_DISPLAY_OFF,
    
    OPERATION_LOG_SET_LEVEL,

    OPERATION_MAGNETOMETER_READ,

    OPERATION_CAMERA_CAPTURE,
} operation_t;

// A struct to represent a command that the command-executor can execute
typedef struct {
    operation_t operation;
    char* p_data;
    size_t len;
    status_t* p_result;
    (*callback)(status_t);
};

#endif // COMMAND_EXECUTOR_H