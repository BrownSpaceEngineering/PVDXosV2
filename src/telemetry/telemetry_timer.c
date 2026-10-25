/**
 * src/telemetry/telemetry_timer.c
 *
 * Periodic timer that asks CFDP to downlink telemetry
 *
 * Created: 20261023 FRI
 * Updated: 20261023 FRI
 * Authors: Noah Shepard
 */

#include "telemetry_timer.h"

#include "FreeRTOS.h"
#include "globals.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "task_list.h"
#include "timers.h"

static StaticTimer_t telemetry_timer_mem;
static TimerHandle_t telemetry_timer_handle = NULL;

/**
 * Runs in the FreeRTOS timer task, whose stack is small, so collecting and serializing telemetry is left to the CFDP
 * task. If the CFDP command queue is full this cycle is skipped
 */
static void telemetry_timer_callback(TimerHandle_t timer) {
    (void)timer;
    command_t cmd = (command_t){
        .target = p_cfdp_task, .data_type = CMD_DATA_NONE, .operation = OPERATION_CFDP_PUT_TELEMETRY, .result = NO_STATUS_RETURN};
    enqueue_command(&cmd);
}

void init_telemetry_timer(void) {
    telemetry_timer_handle = xTimerCreateStatic("Telemetry Timer", pdMS_TO_TICKS(TELEMETRY_PERIOD_MS),
                                                pdTRUE, // auto-reload
                                                NULL, telemetry_timer_callback, &telemetry_timer_mem);

    if (telemetry_timer_handle == NULL) {
        fatal("Failed to create telemetry timer!\n");
    }
}

void start_telemetry_timer(void) {
    if (xTimerStart(telemetry_timer_handle, 0) != pdPASS) {
        warning("telemetry: failed to start telemetry timer\n");
    }
}
