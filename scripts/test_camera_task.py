#!/usr/bin/env python3
"""Host regression tests for the real camera task; no firmware build or hardware.

Run with: python3 scripts/test_camera_task.py -v
Requires a host C compiler (CC, or cc/gcc/clang). All generated files live in a
TemporaryDirectory. Each harness invocation starts with fresh static task state.
"""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]

# Keep the production camera_task.h, globals.h, and arducam_driver.h in use.
# Only their platform dependencies and the unrelated task registry are stubbed.
STUB_HEADERS = {
    "FreeRTOS.h": r"""
#ifndef HOST_FREERTOS_H
#define HOST_FREERTOS_H
#include <stddef.h>
#include <stdint.h>
typedef uint32_t StackType_t;
typedef unsigned int UBaseType_t;
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS pdTRUE
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
typedef struct { unsigned int unused; } StaticQueue_t;
typedef struct { unsigned int unused; } StaticTask_t;
#endif
""",
    "queue.h": r"""
#ifndef HOST_QUEUE_H
#define HOST_QUEUE_H
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreateStatic(UBaseType_t length, UBaseType_t item_size,
                                uint8_t *storage, StaticQueue_t *queue);
BaseType_t xQueueReset(QueueHandle_t queue);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait);
BaseType_t xQueueSendToBack(QueueHandle_t queue, const void *item, TickType_t wait);
#endif
""",
    "task.h": r"""
#ifndef HOST_TASK_H
#define HOST_TASK_H
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
void vPortEnterCritical(void);
void vPortExitCritical(void);
#define taskENTER_CRITICAL() vPortEnterCritical()
#define taskEXIT_CRITICAL() vPortExitCritical()
void vTaskDelay(TickType_t ticks);
void vTaskSuspend(TaskHandle_t task);
void vTaskResume(TaskHandle_t task);
TaskHandle_t xTaskCreateStatic(TaskFunction_t function, const char *name, uint32_t stack_size,
                             void *parameters, UBaseType_t priority, StackType_t *stack, StaticTask_t *tcb);
void vTaskSetThreadLocalStoragePointer(TaskHandle_t task, BaseType_t index, void *value);
#endif
""",
    "semphr.h": r"""
#ifndef HOST_SEMPHR_H
#define HOST_SEMPHR_H
#include "FreeRTOS.h"
typedef struct { BaseType_t available; } StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *storage);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
#endif
""",
    "task_list.h": r"""
#include "globals.h"
extern pvdx_task_t *const p_camera_task;
extern pvdx_task_t *const p_task_manager_task;
extern pvdx_task_t *task_list[];
pvdx_task_t *get_current_task(void);
bool should_checkin(pvdx_task_t *task);
""",
    "logging.h": r"""
void warning(const char *format, ...);
void fatal(const char *format, ...);
void info(const char *format, ...);
void debug(const char *format, ...);
""",
    "SEGGER_RTT.h": r"""
#define SEGGER_RTT_MODE_NO_BLOCK_SKIP 0
int SEGGER_RTT_ConfigUpBuffer(unsigned int channel, const char *name,
                             void *buffer, unsigned int size, unsigned int flags);
""",
    "atmel_start.h": "/* Hardware is not linked into this test. */\n",
    "driver_init.h": "/* Hardware is not linked into this test. */\n",
    "rtos_start.h": "/* No scheduler is started in this test. */\n",
}

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "camera_task.h"
#include "drivers/arducam/arducam_driver.h"
#include "SEGGER_RTT.h"
#include "task_list.h"
#include "semphr.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

camera_task_memory_t camera_mem;
static pvdx_task_t camera_task;
static pvdx_task_t other_task;
pvdx_task_t *const p_camera_task = &camera_task;
static int queue_token;
static QueueHandle_t queue_result = &queue_token;
static unsigned int queue_calls, rtt_calls, init_calls, capture_calls;
static unsigned int warning_calls, fatal_calls, queue_resets;
static int rtt_result;
static status_t init_result = SUCCESS;
static status_t capture_result = SUCCESS;
static bool stop_in_init, stop_in_capture, stop_in_rtt;

SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *storage) {
    storage->available = pdFALSE;
    return storage;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t wait) {
    CHECK(wait == 0 || wait == pdMS_TO_TICKS(3000));
    BaseType_t available = semaphore->available;
    semaphore->available = pdFALSE;
    return available;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    semaphore->available = pdTRUE;
    return pdTRUE;
}
BaseType_t xQueueReset(QueueHandle_t queue) {
    CHECK(queue == queue_result && camera_stop_requested());
    ++queue_resets;
    return pdPASS;
}
void vTaskDelay(TickType_t ticks) { (void)ticks; CHECK(0); }

static void *image_buffer;
static char events[64];
static size_t event_count;

static void event(char code) {
    CHECK(event_count + 1 < sizeof(events));
    events[event_count++] = code;
    events[event_count] = '\0';
}

QueueHandle_t xQueueCreateStatic(UBaseType_t length, UBaseType_t item_size,
                                uint8_t *storage, StaticQueue_t *queue) {
    ++queue_calls;
    CHECK(length == COMMAND_QUEUE_MAX_COMMANDS);
    CHECK(item_size == COMMAND_QUEUE_ITEM_SIZE);
    CHECK(item_size == sizeof(command_t));
    CHECK(storage == camera_mem.camera_command_queue_buffer);
    CHECK(queue == &camera_mem.camera_task_queue);
    return queue_result;
}

void warning(const char *format, ...) {
    CHECK(format != NULL);
    ++warning_calls;
}

void fatal(const char *format, ...) {
    CHECK(format != NULL);
    ++fatal_calls;
}

int SEGGER_RTT_ConfigUpBuffer(unsigned int channel, const char *name,
                             void *buffer, unsigned int size, unsigned int flags) {
    ++rtt_calls;
    event('R');
    CHECK(channel == 2);
    CHECK(channel == CAMERA_RTT_OUTPUT_CHANNEL);
    CHECK(strcmp(name, "Image") == 0);
    CHECK(buffer != NULL);
    CHECK(size == SEGGER_RTT_IMAGE_BUFFER_SIZE);
    CHECK(size == 4096);
    CHECK(flags == SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    if (image_buffer != NULL) {
        CHECK(buffer == image_buffer);
    }
    image_buffer = buffer;
    if (stop_in_rtt) CHECK(!camera_prepare_suspend());
    return rtt_result;
}

status_t init_arducam_hardware(void) {
    ++init_calls;
    event('I');
    CHECK(rtt_calls > 0);
    CHECK(rtt_result == 0);
    if (stop_in_init) CHECK(!camera_prepare_suspend());
    return init_result;
}

status_t capture_rtt_cancellable(arducam_cancel_fn cancelled) {
    CHECK(cancelled == camera_stop_requested);
    CHECK(!cancelled());
    ++capture_calls;
    event('C');
    CHECK(init_calls > 0);
    CHECK(init_result == SUCCESS);
    if (stop_in_capture) {
        CHECK(!camera_prepare_suspend());
        CHECK(cancelled());
        return ERROR_TASK_DISABLED;
    }
    return capture_result;
}

static void counts(unsigned int rtt, unsigned int init, unsigned int capture) {
    CHECK(rtt_calls == rtt);
    CHECK(init_calls == init);
    CHECK(capture_calls == capture);
}

static void request(status_t expected) {
    command_t command = get_camera_capture_command();
    exec_command_camera(&command);
    CHECK(command.result == expected);
}

static void test_command(void) {
    command_t command = get_camera_capture_command();
    CHECK(command.target == p_camera_task);
    CHECK(command.operation == OPERATION_CAMERA_CAPTURE);
    CHECK(command.data_type == CMD_DATA_NONE);
    CHECK(command.data.adcs_data == NULL);
    CHECK(command.data.display_data == NULL);
    CHECK(command.data.task_handle == NULL);
    CHECK(command.data.pvdx_task == NULL);
    CHECK(command.data.cfdp_request == NULL);
    CHECK(command.data.cfdp_timeout_data == NULL);
    CHECK(command.data.state_input_update_data == NULL);
    CHECK(command.result == PROCESSING);
    counts(0, 0, 0);
}

static void test_queue(int fail) {
    if (fail) {
        queue_result = NULL;
    }
    CHECK(init_camera() == queue_result);
    CHECK(queue_calls == 1);
    CHECK(fatal_calls == (unsigned int)fail);
    counts(0, 0, 0);
}

static void test_invalid(const char *kind) {
    command_t command = {
        .target = strcmp(kind, "target") == 0 ? &other_task : p_camera_task,
        .operation = strcmp(kind, "operation") == 0 ? OPERATION_POWER_OFF : OPERATION_CAMERA_CAPTURE,
        .data_type = strcmp(kind, "data") == 0 ? CMD_DATA_DISPLAY : CMD_DATA_NONE,
        .data = {0},
        .result = PROCESSING,
    };
    status_t expected = strcmp(kind, "target") == 0 ? ERROR_BAD_TARGET : ERROR_BAD_ARGS;
    exec_command_camera(&command);
    CHECK(command.result == expected);
    counts(0, 0, 0);
    CHECK(queue_calls == 0);
    CHECK(fatal_calls == 0);

    /* Rejected commands must not mark either resource ready. */
    request(SUCCESS);
    counts(1, 1, 1);
    CHECK(strcmp(events, "RIC") == 0);
}

static void test_rtt_retry(void) {
    rtt_result = -1;
    request(ERROR_NOT_READY);
    counts(1, 0, 0);
    request(ERROR_NOT_READY);
    counts(2, 0, 0);
    CHECK(warning_calls == 2);
    rtt_result = 0;
    request(SUCCESS);
    counts(3, 1, 1);
    request(SUCCESS);
    counts(3, 1, 2);
    CHECK(strcmp(events, "RRRICC") == 0);
}

static void test_init_retry(void) {
    init_result = ERROR_I2C_FAILED;
    request(ERROR_I2C_FAILED);
    counts(1, 1, 0);
    init_result = ERROR_SPI_TRANSFER_FAILED;
    request(ERROR_SPI_TRANSFER_FAILED);
    counts(1, 2, 0);
    CHECK(warning_calls == 2);
    init_result = SUCCESS;
    request(SUCCESS);
    counts(1, 3, 1);
    request(SUCCESS);
    counts(1, 3, 2);
    CHECK(strcmp(events, "RIIICC") == 0);
}

static void test_success(void) {
    request(SUCCESS);
    counts(1, 1, 1);
    request(SUCCESS);
    counts(1, 1, 2);
    CHECK(warning_calls == 0);
    CHECK(strcmp(events, "RICC") == 0);
}

static void test_capture_retry(void) {
    request(SUCCESS);
    capture_result = ERROR_READ_FAILED;
    request(ERROR_READ_FAILED);
    counts(1, 1, 2);
    CHECK(warning_calls == 1);

    /* Recovery must reinitialize hardware, but not the already configured RTT. */
    init_result = ERROR_I2C_FAILED;
    request(ERROR_I2C_FAILED);
    counts(1, 2, 2);
    init_result = SUCCESS;
    capture_result = SUCCESS;
    request(SUCCESS);
    counts(1, 3, 3);
    request(SUCCESS);
    counts(1, 3, 4);
    CHECK(strcmp(events, "RICCIICC") == 0);
}

static void test_cancel(const char *boundary) {
    camera_task.command_queue = init_camera();
    CHECK(camera_task.command_queue != NULL);
    if (strcmp(boundary, "before") == 0) {
        CHECK(!camera_prepare_suspend());
    } else if (strcmp(boundary, "rtt") == 0) {
        stop_in_rtt = true;
    } else if (strcmp(boundary, "capture") == 0) {
        stop_in_capture = true;
    } else {
        stop_in_init = true;
        if (strcmp(boundary, "init_failure") == 0) init_result = ERROR_I2C_FAILED;
    }
    request(ERROR_TASK_DISABLED);
    CHECK(camera_stop_requested());
    unsigned int old_rtt = rtt_calls, old_init = init_calls, old_capture = capture_calls;
    request(ERROR_TASK_DISABLED); // Already queued commands must be rejected as well.
    CHECK(camera_capture() == ERROR_TASK_DISABLED);
    counts(old_rtt, old_init, old_capture);
    if (strcmp(boundary, "capture") != 0) CHECK(capture_calls == 0);
    if (strcmp(boundary, "before") == 0) counts(0, 0, 0);
    if (strcmp(boundary, "rtt") == 0) counts(1, 0, 0);
    if (strncmp(boundary, "init", 4) == 0) counts(1, 1, 0);
    CHECK(queue_resets == 0);
    camera_prepare_resume();
    CHECK(queue_resets == 1 && !camera_stop_requested());
    stop_in_init = stop_in_capture = stop_in_rtt = false;
    init_result = SUCCESS;
    request(SUCCESS);
    CHECK(capture_calls == old_capture + 1);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (strcmp(argv[1], "command") == 0) test_command();
    else if (strcmp(argv[1], "queue") == 0) test_queue(0);
    else if (strcmp(argv[1], "queue_failure") == 0) test_queue(1);
    else if (strcmp(argv[1], "target") == 0 || strcmp(argv[1], "data") == 0 ||
             strcmp(argv[1], "operation") == 0) test_invalid(argv[1]);
    else if (strcmp(argv[1], "rtt_retry") == 0) test_rtt_retry();
    else if (strcmp(argv[1], "init_retry") == 0) test_init_retry();
    else if (strcmp(argv[1], "success") == 0) test_success();
    else if (strcmp(argv[1], "capture_retry") == 0) test_capture_retry();
    else if (strncmp(argv[1], "cancel_", 7) == 0) test_cancel(argv[1] + 7);
    else CHECK(0);
    printf("PASS %s\n", argv[1]);
    return EXIT_SUCCESS;
}
"""


class CameraTaskTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shlex.split(os.environ.get("CC", ""))
        if not compiler:
            compiler = next(([cc] for cc in ("cc", "gcc", "clang") if shutil.which(cc)), [])
        if not compiler or not shutil.which(compiler[0]):
            raise RuntimeError("A host C compiler is required; install cc/gcc/clang or set CC")

        temporary = tempfile.TemporaryDirectory(prefix="camera-task-test-")
        cls.addClassCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        for name, contents in STUB_HEADERS.items():
            (directory / name).write_text(contents, encoding="utf-8")
        harness = directory / "harness.c"
        harness.write_text(HARNESS, encoding="utf-8")
        cls.binary = directory / "camera_task_test"
        command = compiler + [
            "-std=c99", "-Wall", "-Wextra", "-Werror", "-DUNITTEST",
            "-I", str(directory),
            "-I", str(ROOT / "src"),
            "-I", str(ROOT / "src/tasks/camera"),
            str(ROOT / "src/tasks/camera/camera_task.c"),
            str(harness), "-o", str(cls.binary),
        ]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise RuntimeError(
                f"Host compilation failed: {shlex.join(command)}\n{result.stdout}{result.stderr}"
            )

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, msg=f"{case}:\n{result.stdout}{result.stderr}")
        self.assertEqual(result.stdout, f"PASS {case}\n")

    def test_capture_command_fields(self):
        self.run_case("command")

    def test_static_queue_parameters_and_no_hardware_access(self):
        self.run_case("queue")

    def test_queue_failure_logs_fatal(self):
        self.run_case("queue_failure")

    def test_invalid_target_does_not_access_hardware(self):
        self.run_case("target")

    def test_invalid_data_tag_does_not_access_hardware(self):
        self.run_case("data")

    def test_invalid_operation_does_not_access_hardware(self):
        self.run_case("operation")

    def test_nonblocking_channel_two_configuration_retries(self):
        self.run_case("rtt_retry")

    def test_hardware_initialization_failure_propagates_and_retries(self):
        self.run_case("init_retry")

    def test_successful_capture_reuses_ready_resources(self):
        self.run_case("success")

    def test_capture_failure_propagates_and_requires_reinitialization(self):
        self.run_case("capture_retry")

    def test_cancellation_boundaries_and_queued_capture_rejection(self):
        for boundary in ("before", "rtt", "init", "init_failure", "capture"):
            with self.subTest(boundary=boundary):
                self.run_case(f"cancel_{boundary}")


if __name__ == "__main__":
    unittest.main()
