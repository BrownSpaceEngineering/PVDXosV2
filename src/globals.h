/**
 * globals.h
 *
 * Defines global datatypes, structures and headers.
 *
 * Created:
 * Authors: Siddharta Laloux,
 */

#ifndef GLOBALS_H
#define GLOBALS_H

#include <FreeRTOS.h>
#include <queue.h>
#include <stdbool.h>
#include <task.h>

/* ---------- LOGGING CONSTANTS ---------- */

#if defined(RELEASE)
    #define DEFAULT_LOG_LEVEL INFO // The default log level for the system on release builds
#else
    #define DEFAULT_LOG_LEVEL DEBUG // The default log level for the system for debug and unit test builds
#endif

#define SEGGER_RTT_LOG_BUFFER_SIZE 2048 // How big the RTT buffer is for logging (this buffer is flushed to the host regularly)

/* ---------- TASK CONSTANTS ---------- */

#define TASK_STACK_OVERFLOW_PADDING 16            // Buffer for the stack size so that overflow doesn't corrupt any TCBs
#define COMMAND_QUEUE_MAX_COMMANDS 30             // Maximum number of commands that can be queued at once for any task
#define COMMAND_QUEUE_ITEM_SIZE sizeof(command_t) // Size of each item in command queues

/* ---------- ENUMS ---------- */

// An enum to represent the different statuses that a function can return
typedef enum {
    // Standard Responses
    PROCESSING = 0,         // Function is still processing and will return a result later
    NO_STATUS_RETURN,       // Function does not return a status, so don't check it
    SUCCESS,

    // Error Responses
    ERROR_READ_FAILED,
    ERROR_WRITE_FAILED,
    ERROR_SPI_TRANSFER_FAILED,
    ERROR_TASK_DISABLED,
    ERROR_BAD_TARGET,
    ERROR_SANITY_CHECK_FAILED,
    ERROR_PROCESSING_FAILED,
    ERROR_NOT_READY,
} status_t;

// An enum to represent the different operations that tasks can perform (contained within a command_t)
// NOTE: The same operation can have different meanings depending on the target task
typedef enum {
    // General operations (can be overloaded by any task)
    OPERATION_POWER_OFF = 0,

    // Watchdog operations
    OPERATION_CHECKIN, // p_data: TaskHandle_t *handle

    // Task-Manager operations
    OPERATION_INIT_SUBTASKS,   // p_data: NULL
    OPERATION_ENABLE_SUBTASK,  // p_data: TaskHandle_t *handle
    OPERATION_DISABLE_SUBTASK, // p_data: TaskHandle_t *handle

    // Display operations
    OPERATION_DISPLAY_IMAGE,   // p_data: color_t *p_buffer
    OPERATION_CLEAR_IMAGE,     // p_data: NULL

    // Magnetometer & Photodiode operations
    OPERATION_READ,            // p_data: photomag_read_args_t *readings
    OPERATION_PROCESS,         // p_data: TBD

    // TESTING
    TEST_OP, // p_data: char message[]
} operation_t;

// An enum to represent the different log levels that functions can use
typedef enum {
    DEBUG = 0,
    INFO,
    EVENT,
    WARNING,
} log_level_t;

// Enum to represent type of task for state diagram.
typedef enum {
    OS = 0,
    SENSOR,
    ACTUATOR,
    TESTING,
} task_type_t;

/* ---------- MISCELLANEOUS TASK TYPES ---------- */

// A task-initialisation function; takes in nothing and returns a queue handle.
typedef QueueHandle_t (*init_function)(void);

/* ---------- STRUCTS ---------- */

// A struct defining a task's lifecycle in the PVDXos RTOS
typedef struct {
    const char *const name;             // Name of the task
    bool enabled;                       // Whether the task is enabled
    TaskHandle_t handle;                // FreeRTOS handle to the task
    QueueHandle_t command_queue;        // Command queue associated with the task
    const init_function init;           // Initialisation function to call before task entry point
    const TaskFunction_t function;      // Main entry point for the task
    const uint32_t stack_size;          // Size of the stack in words (multiply by 4 to get bytes)
    StackType_t *const stack_buffer;    // Buffer for the stack
    void *pvParameters;                 // Parameters to pass to the task's main function
    UBaseType_t priority;               // Priority of the task in the RTOS scheduler
    StaticTask_t *const task_tcb;       // Task control block
    const uint32_t watchdog_timeout_ms; // How frequently the task should check in with the watchdog (in milliseconds)
    uint32_t last_checkin_time_ticks;   // Last time the task checked in with the watchdog
    bool has_registered;                // Whether the task is being monitored by the watchdog (initialized to NULL)
    const task_type_t task_type;        // Whether the task is OS-integrity, a sensor, or an actuator
} pvdx_task_t;

// A struct to represent a command that OS tasks can execute
typedef struct {
    pvdx_task_t *const target;            // The target task for the command
    const operation_t operation;          // The operation to perform
    const void *const p_data;             // Pointer to data needed for the operation
    const size_t len;                     // Length of the data
    status_t result;                      // Pointer to the result of the operation
    void (*callback)(status_t *p_result); // Callback function to call after the operation is complete
} command_t;

/* ---------- BUILD CONSTANTS ---------- */

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
