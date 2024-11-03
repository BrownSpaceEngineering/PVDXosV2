#ifndef GLOBALS_H
#define GLOBALS_H

#include <freeRTOS.h>

/* --------- HIGH-LEVEL CONFIG VARIABLES AND DEFINES --------- */

#if defined(RELEASE)
    #define DEFAULT_LOG_LEVEL INFO // The default log level for the system on release builds
#else
    #define DEFAULT_LOG_LEVEL INFO // The default log level for the system for debug and unit test builds
#endif

#define SEGGER_RTT_LOG_BUFFER_SIZE 2048 // How big the RTT buffer is for logging (this buffer is flushed to the host regularly)

/* ----------------------------------------------------------- */

#define TASK_STACK_OVERFLOW_PADDING 16            // Buffer for the stack size so that overflow doesn't corrupt any TCBs
#define SUBTASK_START_INDEX 3                     // The index of the first subtask in the task list
#define MINIMUM_HIGH_PRIORITY 128                 // The minimum value for a high-priority operation
#define COMMAND_QUEUE_MAX_COMMANDS 15             // Maximum number of commands that can be queued at once for any task
#define COMMAND_QUEUE_ITEM_SIZE sizeof(command_t) // Size of each item in command queues

typedef enum {
    SUCCESS = 0,

    // Recoverable or low-significance errors are all less than 128
    ERROR_INTERNAL, // Generic error for when something goes wrong
    ERROR_NO_DATA,
    ERROR_NO_MEMORY,
    ERROR_WRITE_FAILED,
    ERROR_NOT_YET_IMPLEMENTED,
    ERROR_RESOURCE_IN_USE, // Similar to EAGAIN in Linux (Basically, this WOULD work but busy rn, try again later)
    ERROR_MAX_SIZE_EXCEEDED,
    ERROR_NULL_HANDLE,
    ERROR_IO,
    ERROR_TIMEOUT,
    
    // High significance errors start at 128 (0x80) (in these cases, restart the system)
    ERROR_UNRECOVERABLE = MINIMUM_HIGH_PRIORITY,
    ERROR_BITFLIP, // Specifically if we detect a bitflip, so we can increment counters.
} status_t;

// An enum to represent the various tasks/daemons that the command dispatcher can interact with
typedef enum {
    TASK_COMMAND_DISPATCHER = 0,
    TASK_MANAGER,
    TASK_DISPLAY,
    // Anything beyond this point is a subtask
    TASK_SHELL,
    TASK_HEARTBEAT,
    TASK_MAGNETOMETER,
    TASK_CAMERA,
    TASK_9AXIS
} task_t;

// An enum to represent the different operations that the command dispatcher can perform
// NOTE: The same operation can have different meanings depending on the target task
typedef enum {
    // General operations (can be overloaded by any task)
    OPERATION_SET_BUFFER = 0,
    OPERATION_UPDATE,
    OPERATION_POWER_OFF,
    // Watchdog specific operations
    OPERATION_REGISTER_TASK,
    OPERATION_UNREGISTER_TASK,
    // Shell-specific operations
    OPERATION_SET_LOG_LEVEL,
    // Task-Manager specific operations
    OPERATION_INIT_SUBTASKS,
    // Anything beyond this point is a high-priority operation
    OPERATION_ENABLE_SUBTASK,
    OPERATION_DISABLE_SUBTASK
} operation_t;

// A struct to represent a command that OS tasks can execute
typedef struct {
    task_t target;
    operation_t operation;
    char* p_data;
    size_t len;
    status_t* p_result;
    void (*callback)(status_t* p_result);
} command_t;

typedef enum {
    DEBUG = 0,
    INFO,
    EVENT,
    WARNING,
} log_level_t;

// Defines for printing out the build version
#if defined(DEVBUILD)
    #define BUILD_TYPE "Development Build"
#endif
#if defined(UNITTEST)
    #define BUILD_TYPE "Unit Test Build"
#endif
#if defined(RELEASE)
    #define BUILD_TYPE "Release Build"
#endif

// Define build date
#define BUILD_DATE __DATE__

// Define build timame
#define BUILD_TIME __TIME__

// IDE-only defines so that the IDE doesn't throw a billion errors for unavailable defines
#if !defined(DEVBUILD) && !defined(UNITTEST) && !defined(RELEASE) // No build flags set so it must be IDE
    #define BUILD_TYPE "<Resolved During Compilation>"
    #define GIT_BRANCH_NAME "<Resolved During Compilation>"
    #define GIT_COMMIT_HASH "<Resolved During Compilation>"
    #define DEVBUILD
    #error "IDE-only defines ran during a build! This should never happen!"
#endif

#endif // GLOBALS_H