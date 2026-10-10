/**
 * states.c
 *
 * Describes the states, inputs, and transitions of the PVDX state machine. The task manager
 * carries out the actual execution of state transitions. 
 *
 * Created: October 9, 2026
 * Authors: Aanya K Agrawal
 */

#include "states.h"

// TRANSITIONS

// these state transitions are just examples
#define LOW_BATTERY_LEVEL 0.1 // absolutely made up
#define RECOVERED_BATTERY_LEVEL 0.5 // absolutely made up

static state_id_t burn_wire_next_state(const input_t *const input) {
    if (input->burn_wire_burned)                    return STATE_POST;
    return STATE_BURN_WIRE;
}

static state_id_t idle_next_state(const input_t *const input) {
    if (input->battery_level < LOW_BATTERY_LEVEL)   return STATE_LOW_POWER_IDLE;
    if (input->image_ready_to_display)              return STATE_STUDENT_PICTURE_CAPTURE;
    if (input->photo_taken_for_downlink)            return STATE_SBAND_TRANS;
    if (input->in_sunlight)                         return STATE_PEROVSKITE_CAPTURE;
    return STATE_IDLE;
}

static state_id_t student_picture_capture_next_state(const input_t *const input) {
    if (input->battery_level < LOW_BATTERY_LEVEL)   return STATE_LOW_POWER_IDLE;
    if (!input->image_ready_to_display)             return STATE_IDLE; // cleared once the picture is taken
    return STATE_STUDENT_PICTURE_CAPTURE;
}

static state_id_t sband_trans_next_state(const input_t *const input) {
    if (input->battery_level < LOW_BATTERY_LEVEL)   return STATE_LOW_POWER_IDLE;
    if (!input->photo_taken_for_downlink)           return STATE_IDLE; // cleared once the downlink finishes
    return STATE_SBAND_TRANS;
}

static state_id_t low_power_idle_next_state(const input_t *const input) {
    if (input->battery_level > RECOVERED_BATTERY_LEVEL) return STATE_IDLE;
    return STATE_LOW_POWER_IDLE;
}

// STATES
// OS tasks are always on and never listed

const state_t mission_states[NUM_STATES] = {
    [STATE_BURN_WIRE]               = {.enabled_tasks = {},                            .fetch_next_state = burn_wire_next_state},
    [STATE_IDLE]                    = {.enabled_tasks = {&adcs_task},                  .fetch_next_state = idle_next_state},
    [STATE_STUDENT_PICTURE_CAPTURE] = {.enabled_tasks = {&adcs_task, &display_task},   .fetch_next_state = student_picture_capture_next_state},
    [STATE_SBAND_TRANS]             = {.enabled_tasks = {&adcs_task, &cfdp_task},      .fetch_next_state = sband_trans_next_state},
    [STATE_LOW_POWER_IDLE]          = {.enabled_tasks = {},                            .fetch_next_state = low_power_idle_next_state},
    // ... one line per state in state_id_t
};


