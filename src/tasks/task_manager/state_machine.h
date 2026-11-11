/**
 * state_machine.h
 *
 * Interface to the part of the task manager that executes the PVDX state machine described in states.c.
 *
 * Created: October 9, 2026
 * Authors: Aanya K Agrawal
 */

#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

// Includes
#include "globals.h"
#include "states.h"
#include "task_list.h"
#include "task_manager_task.h"

// The bit for one input in input_update_data_t.input_update_bitmask
#define INPUT_UPDATE_BIT(update) (1u << (update))

/* ---------- Called by the task manager ---------- */

void state_machine_init(void);
void state_machine_apply_input_update(const input_update_data_t *const p_update);
void state_machine_check_transition(void);
state_id_t state_machine_get_current_state(void);

/* ---------- Called by any task ---------- */

command_t get_state_input_update_command(input_update_data_t *const p_update);

#endif // STATE_MACHINE_H
