#!/usr/bin/env python3
"""Exercise actual camera/main/task-manager lifecycle code with a host scheduler.

Run: python3 scripts/test_camera_lifecycle.py -v
Requires a host C compiler and POSIX ucontext; no firmware or hardware is used.
The single-threaded scheduler deliberately switches at driver/queue/semaphore
boundaries, including preemption immediately on acknowledgement and lifecycle
transitions deferred until the dispatcher's outermost critical-section exit.
"""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

from test_camera_task import ROOT, STUB_HEADERS


HEADERS = dict(STUB_HEADERS)
# Use the real task registry and command dispatcher declarations too.
del HEADERS["task_list.h"]
HEADERS.update({
    "mutexes.h": r"""
#include "semphr.h"
void lock_mutex(SemaphoreHandle_t mutex);
void unlock_mutex(SemaphoreHandle_t mutex);
""",
    "watchdog_task.h": r"""
#include "globals.h"
command_t get_watchdog_checkin_command(pvdx_task_t *task);
void register_task_with_watchdog(pvdx_task_t *task);
void unregister_task_with_watchdog(pvdx_task_t *task);
""",
    "state_machine.h": r"""
#include "globals.h"
void state_machine_init(void);
void state_machine_apply_input_update(input_update_data_t *update);
""",
})

HARNESS = r"""
#define _XOPEN_SOURCE 700
#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include "camera_task.h"
#include "task_manager_task.h"
#include "drivers/arducam/arducam_driver.h"
#include "cmd_dispatcher.h"
#include "watchdog_task.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static int camera_handle, queue_handle, watchdog_queue_handle;
static pvdx_task_t watchdog = {
    .name = "Watchdog", .enabled = true, .command_queue = &watchdog_queue_handle,
};
static pvdx_task_t camera = {
    .name = "Camera", .enabled = true, .handle = &camera_handle,
    .command_queue = &queue_handle, .has_registered = true,
    .prepare_suspend = camera_prepare_suspend, .prepare_resume = camera_prepare_resume,
};
pvdx_task_t *const p_camera_task = &camera;
pvdx_task_t *const p_task_manager_task = NULL;
pvdx_task_t *task_list[] = {&camera, NULL};
task_manager_task_memory_t task_manager_mem;
static StaticSemaphore_t mutex_storage;
SemaphoreHandle_t task_list_mutex = &mutex_storage;
StaticSemaphore_t task_list_mutex_buffer;

static ucontext_t manager_context, camera_context;
static char camera_stack[128 * 1024];
static bool camera_running, locked, hardware_owned, suspended;
static bool preempt_ack, simulate_timeout, fatal_expected;
static const char *boundary;
static unsigned int pending, init_calls, capture_calls, rtt_calls, resets;
static unsigned int acks, suspends, resumes, waits, checkins, driver_pauses;
static unsigned int expected_ack;
static unsigned int admission_checks, late_ack_writes, late_disabled_writes, resume_resets;
static unsigned int critical_depth, critical_enters, critical_exits;
static unsigned int transition_requests, transition_runs, deferred_boundaries, dispatch_writes;
static bool dispatch_boundary_armed, transition_pending, fail_next_send;

static void dispatch_scheduler_boundary(void);

static void check_admission_closed(void);
static void inject_raw_capture(void);
static pvdx_task_t *plain_task;
static unsigned int plain_suspends, plain_resumes;
static bool last_wait_had_ack;
static SemaphoreHandle_t ack_semaphore;
static char fatal_message[512];
static jmp_buf fatal_jump;

static void yield_camera(void) {
    CHECK(camera_running && critical_depth == 0);
    CHECK(swapcontext(&camera_context, &manager_context) == 0);
}

static void run_camera(void) {
    CHECK(!camera_running && !suspended && critical_depth == 0);
    CHECK(camera.has_registered);
    camera_running = true;
    CHECK(swapcontext(&manager_context, &camera_context) == 0);
    camera_running = false;
}

static void camera_entry(void) {
    main_camera(NULL);
    CHECK(0);
}

void warning(const char *format, ...) { CHECK(format != NULL); }
void info(const char *format, ...) { CHECK(format != NULL); }
void debug(const char *format, ...) { CHECK(format != NULL); }
void fatal(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(fatal_message, sizeof(fatal_message), format, args);
    va_end(args);
    CHECK(fatal_expected);
    longjmp(fatal_jump, 1);
}

void vPortEnterCritical(void) {
    ++critical_depth;
    ++critical_enters;
}
void vPortExitCritical(void) {
    CHECK(critical_depth > 0);
    --critical_depth;
    ++critical_exits;
    if (critical_depth == 0) dispatch_scheduler_boundary();
}

void lock_mutex(SemaphoreHandle_t mutex) {
    CHECK(mutex == task_list_mutex && !locked && critical_depth == 0);
    locked = true;
}
void unlock_mutex(SemaphoreHandle_t mutex) {
    CHECK(mutex == task_list_mutex && locked);
    locked = false;
    if (!camera.enabled && camera.has_registered && !camera_stop_requested()) {
        // Exact window between closing admission and requesting the stop handshake.
        CHECK(!suspended);
        check_admission_closed();
    }
}

SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *storage) {
    CHECK(!hardware_owned);
    storage->available = pdFALSE;
    ack_semaphore = storage;
    return storage;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t wait) {
    CHECK(semaphore == ack_semaphore);
    if (wait != 0) {
        CHECK(wait == pdMS_TO_TICKS(3000));
        CHECK(!locked && !camera_running && camera_stop_requested() && critical_depth == 0);
        CHECK(!camera.enabled && camera.has_registered && !suspended);
        check_admission_closed();
        ++waits;
        // A command already dequeued when stop was requested must not touch hardware.
        unsigned int before_init = init_calls, before_capture = capture_calls;
        command_t command = get_camera_capture_command();
        exec_command_camera(&command);
        CHECK(command.result == ERROR_TASK_DISABLED);
        CHECK(init_calls == before_init && capture_calls == before_capture);
        if (!simulate_timeout) run_camera();
        last_wait_had_ack = semaphore->available == pdTRUE;
    }
    BaseType_t available = semaphore->available;
    semaphore->available = pdFALSE;
    return available;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    CHECK(camera_running && semaphore == ack_semaphore);
    CHECK(!hardware_owned && camera_stop_requested());
    CHECK(!locked && pending == 0 && resets > 0);
    CHECK(!camera.enabled && camera.has_registered);
    check_admission_closed();
    // Deliberate raw queue pollution tests the defensive resume reset. This is
    // NOT a dispatcher interleaving: atomic admission now prevents late sends.
    inject_raw_capture();
    ++late_ack_writes;
    semaphore->available = pdTRUE;
    ++acks;
    if (preempt_ack) yield_camera();
    return pdTRUE;
}

QueueHandle_t xQueueCreateStatic(UBaseType_t length, UBaseType_t item_size,
                                uint8_t *storage, StaticQueue_t *queue) {
    CHECK(length == COMMAND_QUEUE_MAX_COMMANDS && item_size == sizeof(command_t));
    CHECK(storage == camera_mem.camera_command_queue_buffer && queue == &camera_mem.camera_task_queue);
    CHECK(!hardware_owned);
    return &queue_handle;
}
BaseType_t xQueueReset(QueueHandle_t queue) {
    CHECK(queue == camera.command_queue && !hardware_owned);
    CHECK(camera_stop_requested() && !camera.enabled && critical_depth == 0);
    if (transition_runs == 1 && resets == 0) {
        // The atomic dispatch write completed before shutdown cleared the queue.
        CHECK(dispatch_writes == 1 && pending == 1);
    }
    if (camera_running) {
        CHECK(!locked && camera.has_registered && !suspended);
    } else {
        CHECK(locked && !camera.has_registered && suspended);
        ++resume_resets;
    }
    pending = 0;
    ++resets;
    return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait) {
    CHECK(camera_running && !hardware_owned && !suspended);
    CHECK(camera.enabled && camera.has_registered);
    CHECK(queue == camera.command_queue);
    CHECK(wait > 0 && wait <= pdMS_TO_TICKS(100));
    if (pending != 0) {
        command_t command = get_camera_capture_command();
        memcpy(item, &command, sizeof(command));
        --pending;
        return pdPASS;
    }
    yield_camera();
    return pdFALSE;
}

void vTaskDelay(TickType_t ticks) {
    CHECK(camera_running && !hardware_owned && !locked);
    CHECK(camera_stop_requested() && ticks == 1 && acks > 0);
    yield_camera();
}
void vTaskSuspend(TaskHandle_t task) {
    CHECK(critical_depth == 0);
    if (plain_task != NULL && task == plain_task->handle) {
        CHECK(locked && plain_task->enabled && plain_task->has_registered);
        ++plain_suspends;
        return;
    }
    CHECK(task == camera.handle && !camera_running && locked);
    CHECK(!hardware_owned && camera_stop_requested());
    CHECK(last_wait_had_ack && acks == expected_ack);
    CHECK(!camera.enabled && camera.has_registered);
    check_admission_closed();
    suspended = true;
    ++suspends;
}
void vTaskResume(TaskHandle_t task) {
    CHECK(critical_depth == 0);
    if (plain_task != NULL && task == plain_task->handle) {
        CHECK(locked && plain_task->enabled && plain_task->has_registered);
        ++plain_resumes;
        return;
    }
    CHECK(task == camera.handle && locked && suspended);
    CHECK(!camera_stop_requested());
    CHECK(camera.enabled && camera.has_registered && pending == 0);
    CHECK(resume_resets == resumes + 1);
    suspended = false;
    ++resumes;
    // Let the actual task preempt immediately, while the manager still holds
    // its mutex. It must reach the next queue wait, not self-suspend after resume.
    run_camera();
}
TaskHandle_t xTaskCreateStatic(TaskFunction_t function, const char *name, uint32_t stack_size,
                             void *parameters, UBaseType_t priority, StackType_t *stack, StaticTask_t *tcb) {
    (void)function; (void)name; (void)stack_size; (void)parameters;
    (void)priority; (void)stack; (void)tcb;
    CHECK(0);
    return NULL;
}
void vTaskSetThreadLocalStoragePointer(TaskHandle_t task, BaseType_t index, void *value) {
    (void)task; (void)index; (void)value; CHECK(0);
}

pvdx_task_t *get_current_task(void) { return &camera; }
bool should_checkin(pvdx_task_t *task) { CHECK(task == &camera); return task->enabled; }
command_t get_watchdog_checkin_command(pvdx_task_t *task) {
    CHECK(task == &camera);
    return (command_t){.target = &watchdog, .operation = OPERATION_CHECKIN};
}
BaseType_t xQueueSendToBack(QueueHandle_t queue, const void *item, TickType_t wait) {
    const command_t *command = item;
    CHECK(wait == 0 && critical_depth > 0);
    if (queue == camera.command_queue) {
        CHECK(command->target == &camera && command->operation == OPERATION_CAMERA_CAPTURE);
        CHECK(camera.enabled);
        if (dispatch_boundary_armed) {
            dispatch_boundary_armed = false;
            transition_pending = true;
            ++transition_requests;
            // A scheduler request at the send boundary must not switch to the
            // manager between the enabled check and the zero-wait queue write.
            dispatch_scheduler_boundary();
            CHECK(transition_pending && transition_runs == 0);
            CHECK(camera.enabled && !camera_stop_requested() && resets == 0 && pending == 0);
        }
        if (fail_next_send) {
            fail_next_send = false;
            return pdFALSE;
        }
        ++pending;
        ++dispatch_writes;
    } else {
        CHECK(queue == watchdog.command_queue && command->operation == OPERATION_CHECKIN);
        CHECK(!camera_stop_requested() && camera.enabled && camera.has_registered);
        // This only models admission to the watchdog queue. Its task cannot
        // process the checkin or take a mutex inside the sender's critical section.
        ++checkins;
    }
    return pdTRUE;
}
static void check_admission_closed(void) {
    CHECK(!camera.enabled && critical_depth == 0);
    unsigned int before = pending;
    command_t command = get_camera_capture_command();
    CHECK(enqueue_command(&command) == ERROR_TASK_DISABLED);
    CHECK(pending == before && critical_depth == 0 && critical_enters == critical_exits);
    ++admission_checks;
}
static void inject_raw_capture(void) {
    // Fault injection bypasses dispatch entirely; all actual queue sends above
    // require the sender's critical section. Keep testing the second reset too.
    CHECK(critical_depth == 0 && !camera.enabled && camera_stop_requested());
    ++pending;
}
void register_task_with_watchdog(pvdx_task_t *task) {
    CHECK(locked && task->enabled && !task->has_registered);
    task->has_registered = true;
}
void unregister_task_with_watchdog(pvdx_task_t *task) {
    CHECK(locked && !task->enabled && task->has_registered);
    if (task == &camera) CHECK(suspended);
    task->has_registered = false;
}
void state_machine_init(void) { CHECK(0); }
void state_machine_apply_input_update(input_update_data_t *update) { (void)update; CHECK(0); }

int SEGGER_RTT_ConfigUpBuffer(unsigned int channel, const char *name,
                             void *buffer, unsigned int size, unsigned int flags) {
    (void)name; (void)buffer; (void)flags;
    CHECK(camera_running && !camera_stop_requested());
    CHECK(channel == CAMERA_RTT_OUTPUT_CHANNEL && size == SEGGER_RTT_IMAGE_BUFFER_SIZE);
    ++rtt_calls;
    return 0;
}
status_t init_arducam_hardware(void) {
    CHECK(camera_running && !camera_stop_requested() && !hardware_owned);
    ++init_calls;
    hardware_owned = true;
    if (strncmp(boundary, "init", 4) == 0 && driver_pauses++ == 0) yield_camera();
    // Simulate driver's full, uncancellable initialization and mutex cleanup.
    CHECK(acks == suspends);
    hardware_owned = false;
    return strcmp(boundary, "init_failure") == 0 ? ERROR_I2C_FAILED : SUCCESS;
}
status_t capture_rtt_cancellable(arducam_cancel_fn cancelled) {
    CHECK(camera_running && cancelled == camera_stop_requested && !cancelled());
    CHECK(!hardware_owned);
    ++capture_calls;
    hardware_owned = true;
    if (strcmp(boundary, "capture") == 0 && driver_pauses++ == 0) yield_camera();
    CHECK(acks == suspends);
    bool stopped = cancelled();
    hardware_owned = false; // Driver deselects SPI before returning.
    return stopped ? ERROR_TASK_DISABLED : SUCCESS;
}

static void test_optional_hooks(void) {
    int handle;
    pvdx_task_t plain = {.name = "Plain", .handle = &handle, .enabled = true, .has_registered = true};
    CHECK(plain.prepare_suspend == NULL && plain.prepare_resume == NULL);
    plain_task = &plain;
    task_manager_disable_task(&plain);
    CHECK(!plain.enabled && !plain.has_registered && !locked);
    task_manager_enable_task(&plain);
    CHECK(plain.enabled && plain.has_registered && !locked);
    CHECK(plain_suspends == 1 && plain_resumes == 1 && waits == 0);
    plain_task = NULL;
}

static void init_camera_context(void) {
    camera.command_queue = init_camera();
    CHECK(!camera_stop_requested() && rtt_calls == 0 && init_calls == 0);
    CHECK(getcontext(&camera_context) == 0);
    camera_context.uc_stack.ss_sp = camera_stack;
    camera_context.uc_stack.ss_size = sizeof(camera_stack);
    camera_context.uc_link = NULL;
    makecontext(&camera_context, camera_entry, 0);
}

static void test_lifecycle(const char *mode) {
    boundary = mode;
    preempt_ack = strstr(mode, "preempt") != NULL;
    simulate_timeout = strcmp(mode, "timeout") == 0;
    if (simulate_timeout) boundary = "capture";
    init_camera_context();
    pending = strcmp(boundary, "idle") == 0 || preempt_ack ? 0 : 4;
    run_camera();
    if (strcmp(boundary, "idle") == 0 || preempt_ack) CHECK(!hardware_owned);
    else CHECK(hardware_owned);

    expected_ack = 1;
    fatal_expected = simulate_timeout;
    if (setjmp(fatal_jump) == 0) {
        task_manager_disable_task(&camera);
        CHECK(!simulate_timeout);
    } else {
        CHECK(simulate_timeout && !locked && suspends == 0 && acks == 0);
        CHECK(!camera.enabled && camera.has_registered && hardware_owned);
        check_admission_closed();
        CHECK(strstr(fatal_message, "Camera") && strstr(fatal_message, "NOT force-suspending"));
        CHECK(strstr(fatal_message, "I2C/SPI"));
        return;
    }
    CHECK(!locked && suspended && !camera.enabled && !camera.has_registered);
    CHECK(acks == 1 && suspends == 1 && waits == 1 && resets == 1 && pending == 1);
    check_admission_closed();
    // Raw queue pollution while suspended must also be discarded on resume.
    inject_raw_capture();
    ++late_disabled_writes;
    CHECK(pending == 2);
    if (strncmp(boundary, "init", 4) == 0) CHECK(capture_calls == 0);
    unsigned int old_capture = capture_calls, old_init = init_calls;
    task_manager_enable_task(&camera);
    CHECK(!locked && !camera_stop_requested() && camera.enabled && camera.has_registered);
    CHECK(pending == 0 && resets == 2 && resume_resets == 1);
    run_camera(); // Continues at semaphore give OR parked delay, without self-suspending.
    CHECK(capture_calls == old_capture && init_calls == old_init && checkins > 0);

    // A stale semaphore token must not satisfy a later stop request. The camera
    // must actually run to a new safe point before the manager can suspend it.
    ack_semaphore->available = pdTRUE;
    expected_ack = 2;
    task_manager_disable_task(&camera);
    CHECK(acks == 2 && suspends == 2 && waits == 2 && resets == 3 && pending == 1);
    check_admission_closed();
    inject_raw_capture();
    ++late_disabled_writes;
    task_manager_enable_task(&camera);
    CHECK(resumes == 2 && !camera_stop_requested() && pending == 0);
    CHECK(resets == 4 && resume_resets == 2);
    CHECK(admission_checks >= 10 && late_ack_writes == 2 && late_disabled_writes == 2);
    run_camera();
    CHECK(capture_calls == old_capture && init_calls == old_init);

    // Only a new explicit request may restart hardware work after resume.
    boundary = "none";
    command_t fresh = get_camera_capture_command();
    CHECK(enqueue_command(&fresh) == SUCCESS && pending == 1);
    run_camera();
    CHECK(pending == 0 && capture_calls == old_capture + 1);
    CHECK(!hardware_owned && !camera_stop_requested());
}

static void dispatch_scheduler_boundary(void) {
    if (!transition_pending) return;
    if (critical_depth != 0) {
        ++deferred_boundaries;
        return;
    }
    CHECK(!camera_running && !locked && camera.enabled && pending == 1);
    CHECK(dispatch_writes == 1 && resets == 0 && suspends == 0 && resumes == 0);
    CHECK(critical_enters == critical_exits);
    transition_pending = false;
    ++transition_runs;
    task_manager_disable_task(&camera);
    CHECK(suspended && !camera.enabled && resets == 1 && capture_calls == 0 && init_calls == 0);
    task_manager_enable_task(&camera);
    CHECK(!suspended && camera.enabled && pending == 0 && resets == 2);
}

static void test_dispatch_transition(bool nested) {
    boundary = "idle";
    init_camera_context();
    run_camera();
    CHECK(pending == 0 && capture_calls == 0 && init_calls == 0);
    expected_ack = 1;
    dispatch_boundary_armed = true;
    command_t command = get_camera_capture_command();
    if (nested) taskENTER_CRITICAL();
    CHECK(enqueue_command(&command) == SUCCESS);
    if (nested) {
        // Exiting the dispatcher's inner critical section must not schedule yet.
        CHECK(critical_depth == 1 && transition_pending && transition_runs == 0);
        CHECK(dispatch_writes == 1 && pending == 1 && camera.enabled && resets == 0);
        taskEXIT_CRITICAL();
    }
    CHECK(!dispatch_boundary_armed && !transition_pending);
    CHECK(transition_requests == 1 && transition_runs == 1 && deferred_boundaries == 1);
    CHECK(dispatch_writes == 1 && acks == 1 && suspends == 1 && resumes == 1);
    CHECK(resets == 2 && pending == 0 && critical_depth == 0 && critical_enters == critical_exits);
    run_camera();
    CHECK(capture_calls == 0 && init_calls == 0 && rtt_calls == 0 && pending == 0);
    // The previously admitted capture was cleared, not delayed until resume.
    CHECK(enqueue_command(&command) == SUCCESS && pending == 1);
    run_camera();
    CHECK(capture_calls == 1 && init_calls == 1 && pending == 0);
}

static void test_dispatch_critical_balance(void) {
    command_t invalid = {.target = NULL};
    CHECK(enqueue_command(&invalid) == ERROR_BAD_TARGET);
    CHECK(critical_enters == 0 && critical_exits == 0);
    command_t command = get_camera_capture_command();
    camera.enabled = false;
    CHECK(enqueue_command(&command) == ERROR_TASK_DISABLED);
    CHECK(critical_enters == 1 && critical_exits == 1 && critical_depth == 0 && pending == 0);
    camera.enabled = true;
    fail_next_send = true;
    CHECK(enqueue_command(&command) == ERROR_QUEUE_ERROR);
    CHECK(!fail_next_send && pending == 0 && critical_enters == 2 && critical_exits == 2);
    CHECK(enqueue_command(&command) == SUCCESS);
    CHECK(pending == 1 && critical_enters == 3 && critical_exits == 3 && critical_depth == 0);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    test_optional_hooks();
    if (strcmp(argv[1], "dispatch_boundary") == 0) test_dispatch_transition(false);
    else if (strcmp(argv[1], "dispatch_nested") == 0) test_dispatch_transition(true);
    else if (strcmp(argv[1], "dispatch_balance") == 0) test_dispatch_critical_balance();
    else test_lifecycle(argv[1]);
    CHECK(critical_depth == 0 && critical_enters == critical_exits);
    printf("PASS %s\n", argv[1]);
    return EXIT_SUCCESS;
}
"""


class CameraLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shlex.split(os.environ.get("CC", ""))
        if not compiler:
            compiler = next(([cc] for cc in ("cc", "gcc", "clang") if shutil.which(cc)), [])
        if not compiler or not shutil.which(compiler[0]):
            raise RuntimeError("A host C compiler is required; install cc/gcc/clang or set CC")
        temporary = tempfile.TemporaryDirectory(prefix="camera-lifecycle-test-")
        cls.addClassCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        for name, content in HEADERS.items():
            (directory / name).write_text(content, encoding="utf-8")
        harness = directory / "harness.c"
        harness.write_text(HARNESS, encoding="utf-8")
        cls.binary = directory / "camera_lifecycle_test"
        command = compiler + [
            "-std=c99", "-Wall", "-Wextra", "-Werror", "-DUNITTEST",
            "-I", str(directory), "-I", str(ROOT / "src"),
            "-I", str(ROOT / "src/tasks"),
            "-I", str(ROOT / "src/tasks/camera"),
            "-I", str(ROOT / "src/tasks/task_manager"),
            "-I", str(ROOT / "src/libraries/cmd_dispatcher"),
            str(ROOT / "src/tasks/camera/camera_task.c"),
            str(ROOT / "src/tasks/camera/camera_main.c"),
            str(ROOT / "src/tasks/task_manager/task_manager_task.c"),
            str(ROOT / "src/libraries/cmd_dispatcher/cmd_dispatcher.c"),
            str(harness), "-o", str(cls.binary),
        ]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise RuntimeError(f"Host compilation failed: {shlex.join(command)}\n{result.stdout}{result.stderr}")

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, msg=f"{case}:\n{result.stdout}{result.stderr}")
        self.assertEqual(result.stdout, f"PASS {case}\n")

    def test_idle_camera_wakes_and_acknowledges(self):
        self.run_case("idle")

    def test_stop_waits_for_full_initialization_and_discards_backlog(self):
        self.run_case("init")

    def test_failed_initialization_returns_before_acknowledgement(self):
        self.run_case("init_failure")

    def test_capture_cancels_and_cleans_up_before_acknowledgement(self):
        self.run_case("capture")

    def test_resume_cannot_be_lost_by_preemption_at_acknowledgement(self):
        self.run_case("idle_preempt")

    def test_timeout_is_fatal_without_forced_suspend_or_unregistration(self):
        self.run_case("timeout")

    def test_dispatch_defers_lifecycle_transition_until_atomic_enqueue_completes(self):
        self.run_case("dispatch_boundary")

    def test_dispatch_critical_sections_balance_on_all_return_paths(self):
        self.run_case("dispatch_balance")

    def test_dispatch_transition_waits_for_outermost_critical_exit(self):
        self.run_case("dispatch_nested")


if __name__ == "__main__":
    unittest.main()
