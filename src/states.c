/**
 * states.c
 *
 * Describes the states, inputs, and transitions of the PVDX state machine. The task manager
 * carries out the actual execution of state transitions.
 *
 * Created: October 9, 2026
 * Authors: Aanya K Agrawal, Thomas Ha
 */

#include "states.h"

#include "cmd_dispatcher.h"
#include "display_task.h"
#include "logging.h"
#include "perovskite_task.h"
#include "state_machine.h"
#include "task_list.h"

// TRANSITIONS

// these state transitions are just examples
#define LOW_BATTERY_LEVEL 0.1                // absolutely made up
#define RECOVERED_BATTERY_LEVEL 0.1          // absolutely made up
#define REFLASH_INTERVAL_MS (60 * 60 * 1000)  // 1 hour
#define REFLASH_START_TIMEOUT_MS 10000        // Give up if reflash_on_enter's input update hasn't arrived by then
// #define FAKE_IDLE_PERIOD_MS 25000            // EXAMPLE ONLY: how long IDLE waits before the next FAKE_HEARTBEAT
// #define FAKE_HEARTBEAT_PERIOD_MS 5000        // EXAMPLE ONLY: how long to stay in FAKE_HEARTBEAT
#define PEROVSKITE_CAPTURE_TIMEOUT_MS 300000 // Give up on a sweep that hasn't finished; a normal one takes about 90 s

// How long the state machine has been in its current state
static TickType_t ticks_in_state(const input_t *const input) {
    return input->current_tick - input->last_transition_tick;
}

// Placeholder for states whose transitions have not been written yet: go straight back to idle
static state_id_t unimplemented_next_state(const input_t *const input) {
    return STATE_IDLE;
}

// EXAMPLE ONLY, disabled: IDLE no longer goes to STATE_FAKE_HEARTBEAT.
// Runs the heartbeat task for a while, then goes back to idle. The heartbeat task mocks the inputs that
// nothing produces yet, so that IDLE goes on to a student picture or a perovskite sweep afterwards (see heartbeat_main.c)
// static state_id_t fake_heartbeat_next_state(const input_t *const input) {
//     if (ticks_in_state(input) >= pdMS_TO_TICKS(FAKE_HEARTBEAT_PERIOD_MS))
//         return STATE_IDLE;
//     return STATE_FAKE_HEARTBEAT;
// }

static state_id_t low_power_idle_next_state(const input_t *const input) {
    if (input->battery_level > RECOVERED_BATTERY_LEVEL)
        return STATE_IDLE;
    return STATE_LOW_POWER_IDLE;
}

static state_id_t idle_next_state(const input_t *const input) {
     /** TODO: Determine priority of transitions */
    if (input->battery_level < LOW_BATTERY_LEVEL)
        return STATE_LOW_POWER_IDLE;
    if (input->uhf_pass_pending)
        return STATE_UHF_PASS;
    if (input->sband_pass_pending)
        return STATE_SBAND_PASS;
    if ((!input->photo_taken_for_downlink && input->in_sunlight) 
        || input->override_take_photo)
        return STATE_PICTURE_CAPTURE;
    if (!input->perovskite_data_ready_for_downlink && input->in_sunlight)
        return STATE_PEROVSKITE_CAPTURE;
    if (input->current_tick - input->last_reflash_tick > pdMS_TO_TICKS(REFLASH_INTERVAL_MS))
        return STATE_REFLASH;
    return STATE_IDLE;
}

static state_id_t perovskite_capture_next_state(const input_t *const input) {
     if (input->perovskite_data_ready_for_downlink || input->uhf_pass_pending || input->sband_pass_pending)
        // TODO: perov task needs to implement on_suspend
        return STATE_IDLE; 
    if (ticks_in_state(input) >= pdMS_TO_TICKS(PEROVSKITE_CAPTURE_TIMEOUT_MS))
        return STATE_IDLE;
    return STATE_PEROVSKITE_CAPTURE;
}

static state_id_t picture_capture_next_state(const input_t *const input) {
    // TODO: picture capture MUST clear override_take_photo before this
    // TODO: add timeout?
    if (input->photo_taken_for_downlink)
        return STATE_IDLE;
    return STATE_PICTURE_CAPTURE;
}

static state_id_t sband_pass_next_state(const input_t *const input) {
    if (!input->sband_pass_pending)
        return STATE_IDLE; // cleared once the downlink finishes
    return STATE_SBAND_PASS;
}

static state_id_t uhf_pass_next_state(const input_t *const input) {
    if (!input->uhf_pass_pending)
        return STATE_IDLE;
    return STATE_UHF_PASS;
}

static state_id_t reflash_next_state(const input_t *const input) {
    // reflash_on_enter's input update goes through the command queue, so it may not have been applied yet. Until it
    // has, reflash_in_progress is still false from before; wait rather than going straight back to IDLE.
    if (input->last_reflash_tick != input->last_transition_tick) {
        if (ticks_in_state(input) >= pdMS_TO_TICKS(REFLASH_START_TIMEOUT_MS))
            return STATE_IDLE; // the update was lost; IDLE will come back here and try again
        return STATE_REFLASH;
    }
    if (!input->reflash_in_progress)
        return STATE_IDLE;
    return STATE_REFLASH;
}

// ENTRY ACTIONS
// Run by the task manager after the new state's tasks have been enabled; see entry_action_t

// Initialises the display, then puts the student picture on it. The display task runs its commands in
// order, so the image is only drawn once initialisation has finished.
static void picture_capture_on_enter(state_id_t previous_state, const input_t *const input) {
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

// Marks a reflash as started and records when, so that IDLE waits another REFLASH_INTERVAL_MS before the next one.
// The entry tick is used as the reflash tick so that reflash_next_state can tell when this update has been applied.
static void reflash_on_enter(state_id_t previous_state, const input_t *const input) {
    // Must be static: the task manager reads it later, when the command comes back round to it
    static input_update_data_t reflash_inputs = {
        .input_update_bitmask = INPUT_UPDATE_BIT(REFLASH_PROGRESS_UPDATE) | INPUT_UPDATE_BIT(LAST_REFLASH_TICK_UPDATE),
    };

    reflash_inputs.new_values.reflash_in_progress = true;
    reflash_inputs.new_values.last_reflash_tick = input->last_transition_tick;

    command_t cmd = get_state_input_update_command(&reflash_inputs);
    const status_t status = enqueue_command(&cmd);
    if (status != SUCCESS) {
        // reflash_next_state gives up after REFLASH_START_TIMEOUT_MS, and IDLE then tries again
        warning("states: failed to enqueue reflash input update (status %d)\n", status);
    }
}

static void perovskite_capture_on_enter(state_id_t previous_state, const input_t *const input) {
    command_t cmd = get_perovskite_sweep_command();
    const status_t status = enqueue_command(&cmd);
    if (status != SUCCESS) {
        warning("states: failed to enqueue perovskite sweep command (status %d)\n", status);
    }
}

// STATES
// OS tasks are always on and never listed. ADCS and the UHF radio run in every state too: the radio task also calibrates
// the RTC that ADCS takes its time from.

state_t mission_states[NUM_STATES];

// The table is filled in at startup rather than at build time so that it can use the p_..._task pointers
void init_states(void) {
    // TODO: remove once every state has its own line below. Until then, a state that is not set up
    // runs only the always-on tasks and goes straight back to idle.
    // The fuel gauge task runs in every state: it supplies battery_level, which LOW_POWER_IDLE needs to recover.
    for (state_id_t state = 0; state < NUM_STATES; state++) {
        mission_states[state] =
            (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task}, .fetch_next_state = unimplemented_next_state};
    }

    // TODO: the thermistor task is a SENSOR task but no state lists it yet, so the state machine suspends it on entering
    // STATE_IDLE and it never runs (no heater control). Add p_thermistor_task to the states that need it.
    mission_states[STATE_UHF_PASS] =
        (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task, p_cfdp_task},
                  .fetch_next_state = uhf_pass_next_state};
    mission_states[STATE_REFLASH] =
        (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task, p_cfdp_task},
                  .fetch_next_state = reflash_next_state,
                  .on_enter = reflash_on_enter};
    mission_states[STATE_IDLE] =
        (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task, p_cfdp_task}, .fetch_next_state = idle_next_state};
    mission_states[STATE_PEROVSKITE_CAPTURE] =
        (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task, p_cfdp_task, p_perovskite_task},
                  .fetch_next_state = perovskite_capture_next_state,
                  .on_enter = perovskite_capture_on_enter};
    mission_states[STATE_PICTURE_CAPTURE] =
        (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task, p_cfdp_task, p_display_task, p_camera_task},
                  .fetch_next_state = student_picture_capture_next_state,
                  .on_enter = student_picture_capture_on_enter};
    mission_states[STATE_SBAND_PASS] = (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task, p_cfdp_task},
                                                  .fetch_next_state = sband_trans_next_state};
    mission_states[STATE_LOW_POWER_IDLE] = (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task, p_cfdp_task},
                                                     .fetch_next_state = low_power_idle_next_state};
    mission_states[STATE_FAKE_HEARTBEAT] =
        (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task, p_cfdp_task, p_heartbeat_task},
                  .fetch_next_state = fake_heartbeat_next_state};
    // ... one line per state in state_id_t
}
