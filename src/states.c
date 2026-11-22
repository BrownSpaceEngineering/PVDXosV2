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
#include "perovskite_task.h"
#include "state_machine.h"
#include "task_list.h"

// TRANSITIONS

// these state transitions are just examples
#define LOW_BATTERY_LEVEL 0.1                // absolutely made up
#define RECOVERED_BATTERY_LEVEL 0.5          // absolutely made up
#define FAKE_IDLE_PERIOD_MS 25000            // EXAMPLE ONLY: how long IDLE waits before the next FAKE_HEARTBEAT
#define FAKE_HEARTBEAT_PERIOD_MS 5000        // EXAMPLE ONLY: how long to stay in FAKE_HEARTBEAT
#define PEROVSKITE_CAPTURE_TIMEOUT_MS 300000 // Give up on a sweep that hasn't finished; a normal one takes about 90 s

// How long the state machine has been in its current state
static TickType_t ticks_in_state(const input_t *const input) {
    return input->current_tick - input->last_transition_tick;
}

// Placeholder for states whose transitions have not been written yet: go straight back to idle
static state_id_t unimplemented_next_state(const input_t *const input) {
    return STATE_IDLE;
}

// The pass state for the pass the ground has open, or STATE_IDLE if there's none
static state_id_t pass_state(const input_t *const input) {
    if (input->sband_pass_pending)
        return STATE_SBAND_PASS;
    if (input->uhf_pass_pending)
        return STATE_UHF_PASS;
    return STATE_IDLE;
}

// Whether a TAKE_PHOTO override is due: its time has come, or UTC isn't known (then it's taken as soon as possible, see
// student_capture_override_on_enter())
static bool student_capture_due(const input_t *const input) {
    if (!input->student_capture_override)
        return false;
    return !input->utc_valid || input->student_capture_time == 0 || (int32_t)(input->current_utc - input->student_capture_time) >= 0;
}

static state_id_t idle_next_state(const input_t *const input) {
    if (input->battery_level < LOW_BATTERY_LEVEL)
        return STATE_LOW_POWER_IDLE;
    // Passes come first: they're short and only happen when the ground is overhead. Other states finish what they're
    // doing first and come back here, which then goes straight to the pass
    if (pass_state(input) != STATE_IDLE)
        return pass_state(input);
    // OVERRIDE, else the previous logic: an uplinked image to show, no photo waiting for downlink, and in eclipse
    if (student_capture_due(input))
        return STATE_STUDENT_CAPTURE_OVERRIDE;
    if (input->image_ready_to_display && !input->photo_taken_for_downlink && !input->in_sunlight)
        return STATE_STUDENT_PICTURE_CAPTURE;
    if (input->in_sunlight && !input->perovskite_data_ready_for_downlink)
        return STATE_PEROVSKITE_CAPTURE;
    if (ticks_in_state(input) >= pdMS_TO_TICKS(FAKE_IDLE_PERIOD_MS))
        return STATE_FAKE_HEARTBEAT; // EXAMPLE ONLY
    return STATE_IDLE;
}

static state_id_t perovskite_capture_next_state(const input_t *const input) {
    if (input->battery_level < LOW_BATTERY_LEVEL)
        return STATE_LOW_POWER_IDLE;
    if (input->perovskite_data_ready_for_downlink)
        return STATE_IDLE; // cleared once the downlink finishes
    if (ticks_in_state(input) >= pdMS_TO_TICKS(PEROVSKITE_CAPTURE_TIMEOUT_MS))
        return STATE_IDLE;
    return STATE_PEROVSKITE_CAPTURE;
}

// EXAMPLE ONLY: runs the heartbeat task for a while, then goes back to idle. The heartbeat task mocks the inputs that
// nothing produces yet, so that IDLE goes on to a student picture or a perovskite sweep afterwards (see heartbeat_main.c)
static state_id_t fake_heartbeat_next_state(const input_t *const input) {
    if (ticks_in_state(input) >= pdMS_TO_TICKS(FAKE_HEARTBEAT_PERIOD_MS))
        return STATE_IDLE;
    return STATE_FAKE_HEARTBEAT;
}

static state_id_t student_picture_capture_next_state(const input_t *const input) {
    // if (input->battery_level < LOW_BATTERY_LEVEL)
    //     return STATE_LOW_POWER_IDLE;
    if (input->photo_taken_for_downlink)
        return STATE_IDLE; // cleared once the picture is taken
    return STATE_STUDENT_PICTURE_CAPTURE;
}

// Leaves once the display task reports the photo taken, which clears the override. Not on photo_taken_for_downlink: an
// earlier photo still waiting for downlink already has it set (the override replaces that photo)
static state_id_t student_capture_override_next_state(const input_t *const input) {
    if (input->battery_level < LOW_BATTERY_LEVEL)
        return STATE_LOW_POWER_IDLE;
    if (!input->student_capture_override)
        return STATE_IDLE;
    return STATE_STUDENT_CAPTURE_OVERRIDE;
}

// Both pass states stay until the UHF task ends the pass, and move straight to the other one if a SYN changes the pass
// type. Leaving suspends CFDP, which first drops its transaction and powers S-band off (cfdp_prepare_suspend())
static state_id_t pass_next_state(const input_t *const input) {
    if (input->battery_level < LOW_BATTERY_LEVEL)
        return STATE_LOW_POWER_IDLE;
    return pass_state(input);
}

static state_id_t low_power_idle_next_state(const input_t *const input) {
    if (input->battery_level > RECOVERED_BATTERY_LEVEL)
        return STATE_IDLE;
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

    // The student's picture saved in MRAM (display_store_image()), or the PVDX logo if none has been saved
    command_t image_cmd = get_display_stored_image_command();
    status = enqueue_command(&image_cmd);
    if (status != SUCCESS) {
        warning("states: failed to enqueue student picture display command (status %d)\n", status);
    }
}

// Same sequence as the student picture state
static void student_capture_override_on_enter(state_id_t previous_state, const input_t *const input) {
    if (!input->utc_valid && input->student_capture_time != 0) {
        warning("states: UTC not set, taking the TAKE_PHOTO picture now instead of at %lu\n", (unsigned long)input->student_capture_time);
    }
    student_picture_capture_on_enter(previous_state, input);
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
    // CFDP only runs in the pass states: transactions only live as long as a pass anyway
    mission_states[STATE_IDLE] =
        (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task}, .fetch_next_state = idle_next_state};
    mission_states[STATE_PEROVSKITE_CAPTURE] =
        (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task, p_perovskite_task},
                  .fetch_next_state = perovskite_capture_next_state,
                  .on_enter = perovskite_capture_on_enter};
    mission_states[STATE_STUDENT_PICTURE_CAPTURE] =
        (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task, p_display_task, p_camera_task},
                  .fetch_next_state = student_picture_capture_next_state,
                  .on_enter = student_picture_capture_on_enter};
    mission_states[STATE_STUDENT_CAPTURE_OVERRIDE] =
        (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task, p_display_task, p_camera_task},
                  .fetch_next_state = student_capture_override_next_state,
                  .on_enter = student_capture_override_on_enter};
    mission_states[STATE_UHF_PASS] =
        (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task, p_cfdp_task}, .fetch_next_state = pass_next_state};
    mission_states[STATE_SBAND_PASS] =
        (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task, p_cfdp_task}, .fetch_next_state = pass_next_state};
    mission_states[STATE_LOW_POWER_IDLE] =
        (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task}, .fetch_next_state = low_power_idle_next_state};
    mission_states[STATE_FAKE_HEARTBEAT] = (state_t){.enabled_tasks = {p_fuel_gauge_task, p_adcs_task, p_uhf_radio_task, p_heartbeat_task},
                                                     .fetch_next_state = fake_heartbeat_next_state};
    // ... one line per state in state_id_t
}
