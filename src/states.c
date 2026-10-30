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

#include "cmd_dispatcher.h"
#include "display_task.h"
#include "logging.h"
#include "state_machine.h"

// TRANSITIONS

// these state transitions are just examples
#define LOW_BATTERY_LEVEL 0.1 // absolutely made up
#define RECOVERED_BATTERY_LEVEL 0.5 // absolutely made up
#define FAKE_HEARTBEAT_PERIOD_MS 10000 // EXAMPLE ONLY: how long to stay in each of IDLE and FAKE_HEARTBEAT

// How long the state machine has been in its current state
static TickType_t ticks_in_state(const input_t *const input) {
    return input->current_tick - input->last_transition_tick;
}

// Placeholder for states whose transitions have not been written yet: go straight back to idle
static state_id_t unimplemented_next_state(const input_t *const input) {
    return STATE_IDLE;
}

static state_id_t initial_next_state(const input_t *const input) {
    return STATE_IDLE;
}

static state_id_t burn_wire_next_state(const input_t *const input) {
    if (input->burn_wire_burned)                    return STATE_POST;
    return STATE_BURN_WIRE;
}

static state_id_t idle_next_state(const input_t *const input) {
    if (input->battery_level < LOW_BATTERY_LEVEL)   return STATE_LOW_POWER_IDLE;
    if (input->image_ready_to_display 
        && !input->photo_taken_for_downlink 
        && !input->in_sunlight)                     return STATE_STUDENT_PICTURE_CAPTURE;
    // if (input->photo_taken_for_downlink)            return STATE_SBAND_TRANS; // TODO: nothing clears photo_taken_for_downlink yet, so SBAND_TRANS never exits
    if (input->in_sunlight)                         return STATE_PEROVSKITE_CAPTURE;
    if (ticks_in_state(input) >= pdMS_TO_TICKS(FAKE_HEARTBEAT_PERIOD_MS)) return STATE_FAKE_HEARTBEAT; // EXAMPLE ONLY
    return STATE_IDLE;
}

// EXAMPLE ONLY: runs the heartbeat task for a while, then goes back to idle
static state_id_t fake_heartbeat_next_state(const input_t *const input) {
    if (ticks_in_state(input) >= pdMS_TO_TICKS(FAKE_HEARTBEAT_PERIOD_MS)) return STATE_IDLE;
    return STATE_FAKE_HEARTBEAT;
}

static state_id_t student_picture_capture_next_state(const input_t *const input) {
    // if (input->battery_level < LOW_BATTERY_LEVEL)   return STATE_LOW_POWER_IDLE;
    if (input->photo_taken_for_downlink)             return STATE_IDLE; // cleared once the picture is taken
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

// ENTRY ACTIONS
// Run by the task manager after the new state's tasks have been enabled; see entry_action_t

// Initialises the display, then puts the student picture on it. The display task runs its commands in
// order, so the image is only drawn once initialisation has finished.
static void student_picture_capture_on_enter(state_id_t previous_state, const input_t *const input) {
    command_t init_cmd = get_display_init_command();
    status_t status = enqueue_command(&init_cmd);
    if (status != SUCCESS) {
        warning("states: failed to enqueue display init command (status %d)\n", status);
        return; // no point drawing on a display that will not be initialised
    }

    // TODO: show the student's picture once there is a buffer for it; PVDX logo for now
    command_t image_cmd = get_display_image_command(IMAGE_BUFFER_PVDX);
    status = enqueue_command(&image_cmd);
    if (status != SUCCESS) {
        warning("states: failed to enqueue student picture display command (status %d)\n", status);
    }
}

// EXAMPLE ONLY: pretends a new student picture has arrived, so that IDLE moves to STUDENT_PICTURE_CAPTURE
// once the heartbeat period is over
static input_update_data_t fake_heartbeat_inputs = {
    .input_update_bitmask = INPUT_UPDATE_BIT(IMAGE_READY_DISPLAY_UPDATE) | INPUT_UPDATE_BIT(PHOTO_READY_DOWNLINK_UPDATE),
    .new_values = {.image_ready_to_display = true, .photo_taken_for_downlink = false},
};

static void fake_heartbeat_on_enter(state_id_t previous_state, const input_t *const input) {
    command_t cmd = get_state_input_update_command(&fake_heartbeat_inputs);
    const status_t status = enqueue_command(&cmd);
    if (status != SUCCESS) {
        warning("states: failed to enqueue fake heartbeat input update (status %d)\n", status);
    }
}

// STATES
// OS tasks are always on and never listed. ADCS runs in every state except STATE_LOW_POWER_IDLE

state_t mission_states[NUM_STATES];

// The table is filled in at startup rather than at build time so that it can use the p_..._task pointers
void init_states(void) {
    // TODO: remove once every state has its own line below. Until then, a state that is not set up
    // runs no tasks and goes straight back to idle.
    // The fuel gauge task runs in every state: it supplies battery_level, which LOW_POWER_IDLE needs to recover.
    for (state_id_t state = 0; state < NUM_STATES; state++) {
        mission_states[state] = (state_t){.enabled_tasks = {p_fuel_gauge_task}, .fetch_next_state = unimplemented_next_state};
    }

    // TODO: the fuel gauge, thermistor and camera tasks are SENSOR tasks but no state lists them yet, so the state
    // machine suspends them on entering STATE_INITIAL and they never run (no battery telemetry, no heater control, no
    // images). Add p_fuel_gauge_task / p_thermistor_task / p_camera_task to the states that need them.
    mission_states[STATE_INITIAL]                 = (state_t){.enabled_tasks = {p_fuel_gauge_task},                                 .fetch_next_state = initial_next_state};
    mission_states[STATE_BURN_WIRE]               = (state_t){.enabled_tasks = {p_fuel_gauge_task},                                 .fetch_next_state = burn_wire_next_state};
    mission_states[STATE_IDLE]                    = (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_cfdp_task},                                 .fetch_next_state = idle_next_state};
    mission_states[STATE_STUDENT_PICTURE_CAPTURE] = (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_cfdp_task, p_display_task, p_camera_task},      .fetch_next_state = student_picture_capture_next_state, .on_enter = student_picture_capture_on_enter};
    mission_states[STATE_SBAND_TRANS]             = (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_cfdp_task},         .fetch_next_state = sband_trans_next_state};
    mission_states[STATE_LOW_POWER_IDLE]          = (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_cfdp_task},                                 .fetch_next_state = low_power_idle_next_state};
    mission_states[STATE_FAKE_HEARTBEAT]          = (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_cfdp_task, p_heartbeat_task},                 .fetch_next_state = fake_heartbeat_next_state, .on_enter = fake_heartbeat_on_enter};
    // ... one line per state in state_id_t
}


