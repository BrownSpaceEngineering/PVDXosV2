/**
 * states.h
 *
 * Describes the states, inputs, and transitions of the PVDX state machine.
 *
 * Created: October 9, 2026
 * Authors: Aanya K Agrawal
 */

#ifndef STATES_H
#define STATES_H

#include "globals.h"
#include "tasks/task_list.h"

#define MAX_ENABLED_TASKS 9

typedef enum { // preliminary contents; still under discussion
    // The burn wire and the other boot steps run in main() before the scheduler starts; the state machine starts in STATE_IDLE
    STATE_FIRST_BOOT,
    STATE_LOW_POWER_IDLE,
    STATE_IDLE,
    STATE_PEROVSKITE_CAPTURE,
    STATE_STUDENT_PICTURE_CAPTURE,
    STATE_STUDENT_CAPTURE_OVERRIDE, // TAKE_PHOTO uplinked: the student picture, whatever the inputs that usually gate it
    STATE_UHF_PASS,
    STATE_SBAND_PASS,
    STATE_FLASH_RECOVERY_CHECK,
    STATE_FAKE_HEARTBEAT, // EXAMPLE ONLY: not a real PVDX state
    NUM_STATES
} state_id_t;

typedef struct { // preliminary contents; still under discussion
    // bool burn_wire_burned;
    bool in_sunlight;
    bool detumbling;     // ?
    float battery_level; // updated by fuel gauge task
    bool image_ready_to_display;
    bool display_active;
    bool photo_taken_for_downlink;
    bool adcs_pointing;     // ?
    bool adcs_sensor_fault; // ?
    bool perovskite_data_ready_for_downlink;
    // TAKE_PHOTO uplinked (sent once the pass ends): take the student picture at student_capture_time. Cleared by the
    // display task once the photo is taken
    bool student_capture_override;
    uint32_t student_capture_time; // Unix time to take it at, 0 for as soon as possible
    // Filled in by the task manager itself; never sent as an input update
    TickType_t current_tick;
    uint32_t current_utc; // Unix time, valid if utc_valid (see utc_get())
    bool utc_valid;
    TickType_t last_transition_tick; // when the current state was entered
    TickType_t last_reflash_tick;
    // Set by the UHF task while the ground has a pass open, from the pass type on its SYN (PUSP_NOTIF_SBAND_PASS). At most
    // one is set. CFDP only runs in STATE_UHF_PASS and STATE_SBAND_PASS
    bool sband_pass_pending;    // UHF + S-band pass: every CFDP PDU goes down on S-band
    bool uhf_pass_pending;      // UHF-only pass
    bool safe_to_disable_radio; // any time this is set to true, the radio should assume it might be disabled
} input_t;

typedef enum { // preliminary contents; still under discussion
    SUNLIGHT_STATUS_UPDATE,
    DETUMBLING_UPDATE,
    BATTERY_LEVEL_UPDATE,
    IMAGE_READY_DISPLAY_UPDATE,
    DISPLAY_ACTIVE_UPDATE,
    PHOTO_READY_DOWNLINK_UPDATE,
    ADCS_POINTING_UPDATE,
    ADCS_SENSOR_FAULT_UPDATE,
    PEROVSKITE_READY_DOWNLINK_UPDATE,
    SBAND_PASS_UPDATE,
    UHF_PASS_UPDATE,
    SAFE_DISABLE_RADIO_UPDATE,
    STUDENT_CAPTURE_OVERRIDE_UPDATE, // student_capture_override and student_capture_time
    NUM_INPUT_UPDATE_OPTIONS
} input_update_t;

struct input_update_data {
    uint32_t input_update_bitmask; // bitmask of input_update_t
    input_t new_values;
};

// cursed transition_checker_t function pointer
typedef state_id_t (*transition_checker_t)(const input_t *const input);

// Runs once, in the task manager, each time a state is entered, after its task list has been applied.
// Must be short and non-blocking, and must not cause a transition itself: to change an input, send an
// input update like any other task. Newly enabled tasks have not run yet, so talk to them via commands.
typedef void (*entry_action_t)(state_id_t previous_state, const input_t *const input);

typedef struct {
    pvdx_task_t *enabled_tasks[MAX_ENABLED_TASKS];
    transition_checker_t fetch_next_state;
    entry_action_t on_enter; // NULL if the state has no entry action
} state_t;

// Filled in by init_states(); read-only after that
extern state_t mission_states[NUM_STATES];

// Must be called once before mission_states is used
void init_states(void);

#endif // STATES_H
