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

#define MAX_ENABLED_TASKS 9

typedef enum {
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
	NUM_STATES
} state_id_t;

typedef struct {
	bool burn_wire_burned;
	bool in_sunlight;
	float battery_level; // ??
	bool image_ready_to_display;
	bool photo_taken_for_downlink;
	int current_tick;
	// etc
} input_t;

typedef enum {
	BURN_WIRE_STATUS_UPDATE,
	SUNLIGHT_STATUS_UPDATE,
	BATTERY_LEVEL_UPDATE,
	IMAGE_READY_DISPLAY_UPDATE,
	PHOTO_READY_DOWNLINK_UPDATE
} input_update_t; // TODO: add a command type for updating the state machine using a bitmask of these updates

// cursed transition_checker_t function pointer
typedef state_id_t (*transition_checker_t)(const input_t* const input);

typedef struct {
	pvdx_task_t *const enabled_tasks[MAX_ENABLED_TASKS]; 
	transition_checker_t fetch_next_state;
} state_t;

extern const state_t mission_states[NUM_STATES];

#endif // STATES_H
