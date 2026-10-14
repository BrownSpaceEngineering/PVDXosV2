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
	STATE_INITIAL,
	STATE_FIRST_BOOT,
	STATE_BURN_WIRE,
	STATE_POST,
	STATE_LOW_POWER_IDLE,
	STATE_LOW_POWER_UHF_TRANS,
	STATE_IDLE,
	STATE_PEROVSKITE_CAPTURE,
	STATE_STUDENT_PICTURE_CAPTURE,
	STATE_SBAND_TRANS,
	STATE_UHF_TRANS,
	STATE_FLASH_RECOVERY_CHECK,
	STATE_FAKE_HEARTBEAT, // EXAMPLE ONLY: not a real PVDX state
	NUM_STATES
} state_id_t;

typedef struct { // preliminary contents; still under discussion
	bool burn_wire_burned;
	bool in_sunlight;
	float battery_level; // not sure yet how this will be stored/fetched
	bool image_ready_to_display;
	bool photo_taken_for_downlink;
	// Filled in by the task manager itself; never sent as an input update
	TickType_t current_tick;
	TickType_t last_transition_tick; // when the current state was entered
	// etc
} input_t;

typedef enum { // preliminary contents; still under discussion
	BURN_WIRE_STATUS_UPDATE,
	SUNLIGHT_STATUS_UPDATE,
	BATTERY_LEVEL_UPDATE,
	IMAGE_READY_DISPLAY_UPDATE,
	PHOTO_READY_DOWNLINK_UPDATE,
	NUM_INPUT_UPDATE_OPTIONS
} input_update_t;

struct input_update_data {
	uint32_t input_update_bitmask; // bitmask of input_update_t 
	input_t new_values;
};

// cursed transition_checker_t function pointer
typedef state_id_t (*transition_checker_t)(const input_t* const input);

typedef struct {
	pvdx_task_t *enabled_tasks[MAX_ENABLED_TASKS];
	transition_checker_t fetch_next_state;
} state_t;

// Filled in by init_states(); read-only after that
extern state_t mission_states[NUM_STATES];

// Must be called once before mission_states is used
void init_states(void);

#endif // STATES_H
