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

#include "task_list.h"

// TRANSITIONS

// these state transitions are just examples
#define LOW_BATTERY_LEVEL 0.1          // absolutely made up
#define RECOVERED_BATTERY_LEVEL 0.5    // absolutely made up
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
    if (input->burn_wire_burned)
        return STATE_POST;
    return STATE_BURN_WIRE;
}

static state_id_t idle_next_state(const input_t *const input) {
    if (input->battery_level < LOW_BATTERY_LEVEL)
        return STATE_LOW_POWER_IDLE;
    if (input->image_ready_to_display)
        return STATE_STUDENT_PICTURE_CAPTURE;
    if (input->photo_taken_for_downlink)
        return STATE_SBAND_TRANS;
    if (input->in_sunlight)
        return STATE_PEROVSKITE_CAPTURE;
    if (ticks_in_state(input) >= pdMS_TO_TICKS(FAKE_HEARTBEAT_PERIOD_MS))
        return STATE_FAKE_HEARTBEAT; // EXAMPLE ONLY
    return STATE_IDLE;
}

// EXAMPLE ONLY: runs the heartbeat task for a while, then goes back to idle
static state_id_t fake_heartbeat_next_state(const input_t *const input) {
    if (ticks_in_state(input) >= pdMS_TO_TICKS(FAKE_HEARTBEAT_PERIOD_MS))
        return STATE_IDLE;
    return STATE_FAKE_HEARTBEAT;
}

static state_id_t student_picture_capture_next_state(const input_t *const input) {
    if (input->battery_level < LOW_BATTERY_LEVEL)
        return STATE_LOW_POWER_IDLE;
    if (!input->image_ready_to_display)
        return STATE_IDLE; // cleared once the picture is taken
    return STATE_STUDENT_PICTURE_CAPTURE;
}

static state_id_t sband_trans_next_state(const input_t *const input) {
    if (input->battery_level < LOW_BATTERY_LEVEL)
        return STATE_LOW_POWER_IDLE;
    if (!input->photo_taken_for_downlink)
        return STATE_IDLE; // cleared once the downlink finishes
    return STATE_SBAND_TRANS;
}

static state_id_t low_power_idle_next_state(const input_t *const input) {
    if (input->battery_level > RECOVERED_BATTERY_LEVEL)
        return STATE_IDLE;
    return STATE_LOW_POWER_IDLE;
}

// STATES
// OS tasks are always on and never listed. ADCS runs in every state too.

state_t mission_states[NUM_STATES];

// The table is filled in at startup rather than at build time so that it can use the p_..._task pointers
void init_states(void) {
    // TODO: remove once every state has its own line below. Until then, a state that is not set up
    // runs only ADCS and goes straight back to idle.
    for (state_id_t state = 0; state < NUM_STATES; state++) {
        mission_states[state] = (state_t){.enabled_tasks = {p_adcs_task}, .fetch_next_state = unimplemented_next_state};
    }

    mission_states[STATE_INITIAL] = (state_t){.enabled_tasks = {p_adcs_task}, .fetch_next_state = initial_next_state};
    mission_states[STATE_BURN_WIRE] = (state_t){.enabled_tasks = {p_adcs_task}, .fetch_next_state = burn_wire_next_state};
    mission_states[STATE_IDLE] = (state_t){.enabled_tasks = {p_adcs_task}, .fetch_next_state = idle_next_state};
    mission_states[STATE_STUDENT_PICTURE_CAPTURE] =
        (state_t){.enabled_tasks = {p_adcs_task, p_display_task}, .fetch_next_state = student_picture_capture_next_state};
    mission_states[STATE_SBAND_TRANS] = (state_t){.enabled_tasks = {p_adcs_task, p_cfdp_task}, .fetch_next_state = sband_trans_next_state};
    mission_states[STATE_LOW_POWER_IDLE] = (state_t){.enabled_tasks = {p_adcs_task}, .fetch_next_state = low_power_idle_next_state};
    mission_states[STATE_FAKE_HEARTBEAT] =
        (state_t){.enabled_tasks = {p_adcs_task, p_heartbeat_task}, .fetch_next_state = fake_heartbeat_next_state};
    // ... one line per state in state_id_t
}
