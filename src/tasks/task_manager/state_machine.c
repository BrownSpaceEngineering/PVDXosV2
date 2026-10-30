/**
 * state_machine.c
 *
 * Executes the PVDX state machine described in states.c.
 *
 * Created: October 9, 2026
 * Authors: Aanya K Agrawal
 */

#include "state_machine.h"

#include "logging.h"

// Whether state_machine_init() has run; no transition is taken before then
static bool state_machine_initialised = false;
static state_id_t current_state = STATE_INITIAL;
// Current inputs; only written by task manager.
// TODO: battery_level starts full so that the example low-battery transitions in states.c do not
// fire before anything has reported a real value
static input_t inputs = {.battery_level = 1.0f};

/* ---------- HELPERS ---------- */

/**
 * \fn state_machine_controls_task
 *
 * \brief Returns whether the state machine is allowed to enable/disable the given task.
 *      OS and TESTING tasks are not part of the state machine.
 */
static bool state_machine_controls_task(const pvdx_task_t *const p_task) {
    return p_task->task_type == SENSOR || p_task->task_type == ACTUATOR;
}

/**
 * \fn state_enables_task
 *
 * \brief Returns whether the given task is listed in the given state's `enabled_tasks`
 */
static bool state_enables_task(state_id_t state, const pvdx_task_t *const p_task) {
    for (size_t i = 0; i < MAX_ENABLED_TASKS; i++) {
        if (mission_states[state].enabled_tasks[i] == p_task) {
            return true;
        }
    }
    return false;
}

/**
 * \fn enter_state
 *
 * \brief Enables/disables tasks so that exactly the new state's `enabled_tasks` are running, records
 *      the new state, then runs its `on_enter` action if it has one. OS and TESTING tasks are not part
 *      of the state machine and are left alone.
 *
 * \warning every task must already have been initialised by the task manager
 */
static void enter_state(state_id_t next_state) {
#if defined(DEVBUILD) || defined(UNITTEST)
    // A state listing a task the state machine does not control (e.g. an OS task) is a mistake in states.c
    for (size_t i = 0; i < MAX_ENABLED_TASKS; i++) {
        const pvdx_task_t *const p_listed_task = mission_states[next_state].enabled_tasks[i];
        if (p_listed_task != NULL && !state_machine_controls_task(p_listed_task)) {
            warning("state_machine: state %d lists task %s, which the state machine does not control\n", next_state, p_listed_task->name);
        }
    }
#endif

    for (size_t i = 0; task_list[i] != NULL; i++) {
        pvdx_task_t *const p_task = task_list[i];
        if (!state_machine_controls_task(p_task)) {
            continue;
        }

        const bool wanted = state_enables_task(next_state, p_task);
        if (wanted && !p_task->enabled) {
            task_manager_enable_task(p_task);
        } else if (!wanted && p_task->enabled) {
            task_manager_disable_task(p_task);
        }
    }

    info("state_machine: state %d -> state %d\n", current_state, next_state);
    const state_id_t previous_state = current_state;
    current_state = next_state;
    inputs.last_transition_tick = xTaskGetTickCount();

    if (mission_states[next_state].on_enter != NULL) {
        mission_states[next_state].on_enter(previous_state, &inputs);
    }
}

/**
 * \fn copy_input
 *
 * \brief Copies the one input named by `update` from `p_new_values` into the task manager's inputs.
 *      This is the one place that maps an `input_update_t` onto its `input_t` field.
 *
 * \warning has no `default` case on purpose: the build fails if an `input_update_t` is added
 *      without being handled here
 */
static void copy_input(input_update_t update, const input_t *const p_new_values) {
    switch (update) {
        case BURN_WIRE_STATUS_UPDATE:
            inputs.burn_wire_burned = p_new_values->burn_wire_burned;
            break;
        case SUNLIGHT_STATUS_UPDATE:
            inputs.in_sunlight = p_new_values->in_sunlight;
            break;
        case DETUMBLING_UPDATE:
            inputs.detumbling = p_new_values->detumbling;
            break;
        case BATTERY_LEVEL_UPDATE:
            inputs.battery_level = p_new_values->battery_level;
            break;
        case IMAGE_READY_DISPLAY_UPDATE:
            inputs.image_ready_to_display = p_new_values->image_ready_to_display;
            break;
        case DISPLAY_ACTIVE_UPDATE:
            inputs.display_active = p_new_values->display_active;
            break;
        case PHOTO_READY_DOWNLINK_UPDATE:
            inputs.photo_taken_for_downlink = p_new_values->photo_taken_for_downlink;
            break;
        case PEROVSKITE_READY_DOWNLINK_UPDATE:
            inputs.perovskite_data_ready_for_downlink = p_new_values->perovskite_data_ready_for_downlink;
            break;
        case SBAND_READY_TRANSMIT_UPDATE:
            inputs.sband_wants_to_transmit = p_new_values->sband_wants_to_transmit;
            break;
        case SAFE_DISABLE_RADIO_UPDATE:
            inputs.safe_to_disable_radio = p_new_values->safe_to_disable_radio;
            break;
        case NUM_INPUT_UPDATE_OPTIONS:
            // Not an input; listed so that the switch covers the whole enum
            break;
    }
}

/* ---------- CALLED BY THE TASK MANAGER ---------- */

/**
 * \fn state_machine_init
 *
 * \brief Sets up the state table and enters STATE_INITIAL
 *
 * \warning must be called after `task_manager_init_subtasks()`
 */
void state_machine_init(void) {
    init_states();
    enter_state(STATE_INITIAL);
    state_machine_initialised = true;
}

/**
 * \fn state_machine_apply_input_update
 *
 * \brief Copies every input whose bit is set in `p_update->input_update_bitmask` from
 *      `p_update->new_values` into the task manager's inputs, then checks for a transition
 *
 * \param p_update the update carried by an OPERATION_UPDATE_STATE_INPUTS command
 */
void state_machine_apply_input_update(const input_update_data_t *const p_update) {
    if (p_update == NULL) {
        fatal("state_machine: input update with no data\n");
    }

    for (input_update_t update = 0; update < NUM_INPUT_UPDATE_OPTIONS; update++) {
        if (p_update->input_update_bitmask & INPUT_UPDATE_BIT(update)) {
            copy_input(update, &p_update->new_values);
        }
    }

    state_machine_check_transition();
}

/**
 * \fn state_machine_check_transition
 *
 * \brief Asks the current state's transition function which state to be in, and moves there if it
 *      differs. Called once per task manager loop so that time-based transitions fire without an
 *      input update.
 */
void state_machine_check_transition(void) {
    if (!state_machine_initialised) {
        warning("state_machine: transition check attempted before state_machine_init()\n");
        return;
    }

    // Inputs the task manager knows itself are filled in here rather than sent as updates
    inputs.current_tick = xTaskGetTickCount();

    const state_id_t next_state = mission_states[current_state].fetch_next_state(&inputs);
    if (next_state >= NUM_STATES) {
        fatal("state_machine: state %d asked to move to invalid state %d\n", current_state, next_state);
    }

    if (next_state != current_state) {
        enter_state(next_state);
    }
}

/* ---------- CALLED BY ANY TASK ---------- */

/**
 * \fn get_state_input_update_command
 *
 * \brief Builds the command that tells the task manager a task's state machine inputs have changed
 *
 * \param p_update the inputs this task owns. Must be static: the task manager reads it later,
 *      from its own task. Each task keeps ONE of these, whose bitmask lists the inputs that task
 *      owns and never changes afterwards; the task only edits `new_values` and sends again:
 *
 *          static input_update_data_t arducam_inputs = {
 *              .input_update_bitmask = INPUT_UPDATE_BIT(PHOTO_READY_DOWNLINK_UPDATE),
 *          };
 *
 *          arducam_inputs.new_values.photo_taken_for_downlink = true;
 *          command_t cmd = get_state_input_update_command(&arducam_inputs);
 *          enqueue_command(&cmd);
 *
 * \returns command_t command structure
 */
command_t get_state_input_update_command(input_update_data_t *const p_update) {
    return (command_t){
        .target = p_task_manager_task,
        .operation = OPERATION_UPDATE_STATE_INPUTS,
        .data.state_input_update_data = p_update,
        .data_type = CMD_STATE_INPUT_UPDATE,
        .result = NO_STATUS_RETURN,
    };
}
