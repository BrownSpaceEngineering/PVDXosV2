/**
 * perovskite_task.c
 *
 * Measures the perovskite pixels: on each OPERATION_PEROVSKITE_SWEEP, steps every pixel through
 * PEROVSKITE_STEP_COUNT voltages, logs and records the current at each step, and saves the sweep to MRAM.
 *
 * A sweep takes about 90 s: each step waits out two INA226 conversions, see ina226_wait_for_fresh_conversion().
 */

#include "perovskite_task.h"

#include <string.h>

#include "cmd_dispatcher.h"
#include "drivers/mram/mram_map.h"
#include "drivers/pmb/ina226.h"
#include "drivers/pmb/pvd_dac.h"
#include "logging.h"
#include "os_delay.h"
#include "semphr.h"
#include "state_machine.h"
#include "task_list.h"
#include "utc.h"
#include "watchdog_task.h"

#define PEROVSKITE_SETTLE_MS 5U                                             // Opamp settling time after a DAC step
#define PEROVSKITE_CONVERSION_TIMEOUT_MS (4U * INA226_CONVERSION_PERIOD_MS) // Two conversions plus margin
#define PEROVSKITE_CHECKIN_STEPS 16U                                        // Steps between watchdog check-ins (about 1.5 s)

_Static_assert(MRAM_RECORD_OVERHEAD + sizeof(perovskite_sweep_t) <= PEROVSKITE_SLOT_SIZE, "a sweep must fit its slot");
_Static_assert(PEROVSKITE_SLOT_SIZE * PEROVSKITE_SLOT_COUNT <= MRAM_PEROVSKITE_SIZE, "sweeps must fit their MRAM region");

static volatile bool stop_requested = false;
static StaticSemaphore_t stop_ack_storage;
static SemaphoreHandle_t stop_ack;

static bool hardware_ready = false;
static bool slots_scanned = false; // Whether next_slot and next_sequence have been found from MRAM
static uint8_t next_slot = 0;
static uint32_t next_sequence = 0;

static perovskite_sweep_t sweep; // The sweep being measured, or a saved one while scanning the slots

// The state machine input this task owns (static: the task manager reads it after the command is sent)
static input_update_data_t perovskite_inputs = {
    .input_update_bitmask = INPUT_UPDATE_BIT(PEROVSKITE_READY_DOWNLINK_UPDATE),
};

/* ---------- TASK LIFECYCLE ---------- */

QueueHandle_t init_perovskite(void) {
    stop_ack = xSemaphoreCreateBinaryStatic(&stop_ack_storage);
    if (stop_ack == NULL) {
        fatal("perovskite: Failed to create lifecycle acknowledgement semaphore!\n");
    }
    QueueHandle_t queue = xQueueCreateStatic(PEROVSKITE_QUEUE_LENGTH, COMMAND_QUEUE_ITEM_SIZE, perovskite_mem.perovskite_command_queue_buffer,
                                             &perovskite_mem.perovskite_task_queue);
    if (queue == NULL) {
        fatal("perovskite: Failed to create command queue!\n");
    }
    return queue;
}

bool perovskite_stop_requested(void) {
    return stop_requested;
}

bool perovskite_prepare_suspend(void) {
    if (stop_ack == NULL) {
        return false;
    }
    // Drain the previous handshake before publishing a new request.
    (void)xSemaphoreTake(stop_ack, 0);
    stop_requested = true;
    return xSemaphoreTake(stop_ack, pdMS_TO_TICKS(3000)) == pdTRUE;
}

void perovskite_prepare_resume(void) {
    // Drop late queue writes from producers that passed admission before disable.
    xQueueReset(p_perovskite_task->command_queue);
    stop_requested = false;
}

void perovskite_park_if_stopped(void) {
    if (!perovskite_stop_requested()) {
        return;
    }

    // Only called by main_perovskite, between sweeps, so the circuit is idle and the buses are free.
    xQueueReset(p_perovskite_task->command_queue);
    xSemaphoreGive(stop_ack);
    // Never self-suspend: the manager could resume us just before a self-suspend.
    while (perovskite_stop_requested()) {
        os_delay_ms(1);
    }
}

void perovskite_checkin(void) {
    command_t cmd_checkin = get_watchdog_checkin_command(p_perovskite_task);
    if (should_checkin(p_perovskite_task) && enqueue_command(&cmd_checkin) != SUCCESS) {
        warning("perovskite: Failed to enqueue watchdog checkin command\n");
    }
}

command_t get_perovskite_sweep_command(void) {
    return (command_t){
        .target = p_perovskite_task,
        .operation = OPERATION_PEROVSKITE_SWEEP,
        .data = {0},
        .data_type = CMD_DATA_NONE,
        .result = PROCESSING,
    };
}

/* ---------- MRAM ---------- */

static uint32_t perovskite_slot_offset(uint8_t slot) {
    return (uint32_t)slot * PEROVSKITE_SLOT_SIZE;
}

status_t perovskite_load_sweep(uint8_t slot, perovskite_sweep_t *p_sweep) {
    if (slot >= PEROVSKITE_SLOT_COUNT) {
        return ERROR_BAD_ARGS;
    }
    if (!mram_record_load(MRAM_REGION_PEROVSKITE, perovskite_slot_offset(slot), p_sweep, sizeof(*p_sweep))) {
        return ERROR_READ_FAILED;
    }
    return SUCCESS;
}

/**
 * Finds where the next sweep goes, from the sequence numbers already in MRAM: the slot after the newest sweep, which
 * holds the oldest. Uses the sweep buffer, so it must not run during a sweep.
 */
static status_t perovskite_scan_slots(void) {
#ifdef PEROVSKITE_LOG_ONLY
    info("perovskite: PEROVSKITE_LOG_ONLY is set, sweeps are logged but not saved to MRAM\n");
    slots_scanned = true;
    return SUCCESS;
#else
    bool found = false;
    uint32_t newest_sequence = 0;
    uint8_t newest_slot = 0;

    for (uint8_t slot = 0; slot < PEROVSKITE_SLOT_COUNT; slot++) {
        if (perovskite_stop_requested()) {
            return ERROR_TASK_DISABLED;
        }
        if (perovskite_load_sweep(slot, &sweep) == SUCCESS && (!found || sweep.sequence > newest_sequence)) {
            found = true;
            newest_sequence = sweep.sequence;
            newest_slot = slot;
        }
    }

    next_slot = found ? (newest_slot + 1U) % PEROVSKITE_SLOT_COUNT : 0;
    next_sequence = found ? newest_sequence + 1U : 0;
    slots_scanned = true;
    info("perovskite: next sweep is #%lu in MRAM slot %u\n", (unsigned long)next_sequence, next_slot);
    return SUCCESS;
#endif
}

static status_t perovskite_save_sweep(void) {
#ifdef PEROVSKITE_LOG_ONLY
    return SUCCESS;
#else
    if (!mram_record_save(MRAM_REGION_PEROVSKITE, perovskite_slot_offset(next_slot), &sweep, sizeof(sweep))) {
        return ERROR_WRITE_FAILED;
    }
    return SUCCESS;
#endif
}

/* ---------- MEASUREMENT ---------- */

// Called only by the perovskite task, after the scheduler starts. Retried on the next sweep after any failure.
static status_t perovskite_init_hardware(void) {
    status_t status = ina226_init();
    if (status != SUCCESS) {
        return status;
    }
    // Also brings up the MCP23017, and leaves every pixel grounded
    if ((status = adg734_init()) != SUCCESS) {
        return status;
    }
    return pvd_dac_init();
}

static void perovskite_idle_circuit(void) {
    if (pvd_dac_set_code(0) != SUCCESS || adg734_deselect_all() != SUCCESS) {
        warning("perovskite: Failed to idle the measurement circuit\n");
        hardware_ready = false;
    }
}

static status_t perovskite_measure_step(uint8_t pixel, uint32_t step, int16_t *p_current) {
    const uint32_t step_mv = PEROVSKITE_STEP_MV(step, PVD_PIXEL_FULL_SCALE_MV);
    status_t status = pvd_dac_set_pixel_mv(step_mv);
    if (status != SUCCESS) {
        return status;
    }
    os_delay_ms(PEROVSKITE_SETTLE_MS);
    if ((status = ina226_wait_for_fresh_conversion(PEROVSKITE_CONVERSION_TIMEOUT_MS)) != SUCCESS) {
        return status;
    }
    int32_t current_na = 0;
    if ((status = ina226_read_current_na(&current_na)) != SUCCESS) {
        return status;
    }
    *p_current = (int16_t)(current_na / INA226_CURRENT_LSB_NA);
    info("perovskite: px %2u step %2lu set %4lu mV I %8ld nA\n", pixel, (unsigned long)step, (unsigned long)step_mv, (long)current_na);
    return SUCCESS;
}

/**
 * Sweeps one pixel upward from 0 V into p_samples, and leaves the circuit idle whether or not it finished. A step that
 * fails is recorded as PEROVSKITE_SAMPLE_INVALID, and the sweep carries on.
 */
static status_t perovskite_sweep_pixel(uint8_t pixel, int16_t *p_samples) {
    uint8_t selected = PMB_PIXEL_NONE;
    status_t status = adg734_select_pixel(pixel);
    if (status == SUCCESS) {
        status = adg734_read_selected(&selected);
    }
    const bool pixel_routed = status == SUCCESS && selected == pixel;
    if (!pixel_routed) {
        warning("perovskite: Failed to select pixel %u (status=%d, selected=%u)\n", pixel, status, selected);
        hardware_ready = false;
    }

    status_t result = SUCCESS;
    for (uint32_t step = 0; step < PEROVSKITE_STEP_COUNT; step++) {
        if (perovskite_stop_requested()) {
            result = ERROR_TASK_DISABLED;
            break;
        }
        if (step % PEROVSKITE_CHECKIN_STEPS == 0) {
            perovskite_checkin();
        }

        status = pixel_routed ? perovskite_measure_step(pixel, step, &p_samples[step]) : ERROR_NOT_READY;
        if (status != SUCCESS) {
            p_samples[step] = PEROVSKITE_SAMPLE_INVALID;
            sweep.failed_steps++;
            if (pixel_routed) {
                warning("perovskite: px %u step %lu failed (status=%d)\n", pixel, (unsigned long)step, status);
                hardware_ready = false;
            }
        }
    }

    perovskite_idle_circuit();
    return result;
}

/**
 * Measures a whole sweep and saves it to the slot at next_slot. Only moves on to the next slot if the sweep was saved:
 * a stop or reboot partway leaves MRAM untouched.
 */
static status_t perovskite_run_sweep(void) {
    uint32_t unix_time = 0;

    memset(&sweep, 0, sizeof(sweep));
    sweep.sequence = next_sequence;
    sweep.timestamp = utc_get(&unix_time) == SUCCESS ? unix_time : 0;
    sweep.full_scale_mv = PVD_PIXEL_FULL_SCALE_MV;

    for (uint8_t pixel = 0; pixel < PEROVSKITE_PIXEL_COUNT; pixel++) {
        status_t status = perovskite_sweep_pixel(pixel, sweep.current[pixel]);
        if (status != SUCCESS) {
            return status; // Stopped partway. Only complete sweeps are kept
        }
        perovskite_checkin();
    }

    if (sweep.failed_steps == PEROVSKITE_PIXEL_COUNT * PEROVSKITE_STEP_COUNT) {
        warning("perovskite: Every step failed, not keeping the sweep\n");
        return ERROR_PROCESSING_FAILED;
    }

    status_t status = perovskite_save_sweep();
    if (status != SUCCESS) {
        return status;
    }
    next_slot = (next_slot + 1U) % PEROVSKITE_SLOT_COUNT;
    next_sequence++;
    return SUCCESS;
}

/**
 * TODO: downlink perovskite data. telemetry.h has no perovskite fields yet, and a sweep (about 2 KB) doesn't fit in the
 * 512-byte downlink packet, so this needs its own packet format split over several packets (read back with
 * perovskite_load_sweep()). Sweeps from before a reboot also need sending, so track which sequence numbers
 * have been sent. Once everything is sent, set perovskite_data_ready_for_downlink back to false, which is what allows
 * the next sweep.
 */
static void perovskite_send_telemetry(const perovskite_sweep_t *p_sweep) {
    (void)p_sweep;
}

// Called only by the perovskite task. Hardware is left alone while the task is disabled.
static status_t perovskite_capture(void) {
    if (!hardware_ready) {
        status_t status = perovskite_init_hardware();
        if (status != SUCCESS) {
            warning("perovskite: Hardware initialization failed (status=%d)\n", status);
            return status;
        }
        hardware_ready = true;
    }
    if (!slots_scanned) {
        status_t status = perovskite_scan_slots();
        if (status != SUCCESS) {
            return status;
        }
    }
    perovskite_checkin();

    info("perovskite: Starting sweep #%lu\n", (unsigned long)next_sequence);
    status_t status = perovskite_run_sweep();
    if (status != SUCCESS) {
        if (status != ERROR_TASK_DISABLED) {
            warning("perovskite: Sweep not saved (status=%d)\n", status);
        }
        return status;
    }
    info("perovskite: Sweep #%lu done, %u steps failed\n", (unsigned long)sweep.sequence, sweep.failed_steps);

    perovskite_send_telemetry(&sweep);

    perovskite_inputs.new_values.perovskite_data_ready_for_downlink = true;
    command_t cmd_ready = get_state_input_update_command(&perovskite_inputs);
    if (enqueue_command(&cmd_ready) != SUCCESS) {
        warning("perovskite: Failed to enqueue data ready update\n");
    }
    return SUCCESS;
}

// Logs how close the task has come to overflowing its stack. A sweep goes through the deepest calls (MRAM reads and
// writes, logging), so this is checked after each one.
static void perovskite_log_stack_usage(void) {
    const UBaseType_t free_words = uxTaskGetStackHighWaterMark(NULL);
    if (free_words < PEROVSKITE_TASK_STACK_SIZE / 4U) {
        warning("perovskite: stack nearly full: %lu of %u words never used\n", (unsigned long)free_words, PEROVSKITE_TASK_STACK_SIZE);
    } else {
        info("perovskite: stack %lu of %u words never used\n", (unsigned long)free_words, PEROVSKITE_TASK_STACK_SIZE);
    }
}

void exec_command_perovskite(command_t *const p_cmd) {
    if (p_cmd->target != p_perovskite_task) {
        warning("perovskite: Command addressed to another task\n");
        p_cmd->result = ERROR_BAD_TARGET;
        return;
    }

    switch (p_cmd->operation) {
        case OPERATION_PEROVSKITE_SWEEP:
            if (perovskite_stop_requested()) {
                p_cmd->result = ERROR_TASK_DISABLED;
                break;
            }
            if (p_cmd->data_type != CMD_DATA_NONE) {
                p_cmd->result = ERROR_BAD_ARGS;
                break;
            }
            p_cmd->result = perovskite_capture();
            perovskite_log_stack_usage();
            break;
        default:
            warning("perovskite: Unsupported operation %d\n", p_cmd->operation);
            p_cmd->result = ERROR_BAD_ARGS;
            break;
    }
}
