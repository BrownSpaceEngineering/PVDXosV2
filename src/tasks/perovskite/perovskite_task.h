#ifndef PEROVSKITE_TASK_H
#define PEROVSKITE_TASK_H

#include "drivers/pmb/adg734.h"
#include "globals.h"

#define PEROVSKITE_TASK_STACK_SIZE 512 // Stack size in words
#define PEROVSKITE_QUEUE_LENGTH 4      // The only command is one sweep request per state entry

#define PEROVSKITE_PIXEL_COUNT PMB_PIXEL_COUNT
#define PEROVSKITE_STEP_COUNT 64U    // Most voltage steps per pixel (the sampling resolution's maximum and default)
#define PEROVSKITE_MIN_STEP_COUNT 2U // Fewest: 0 V and full scale

// FOLLOW voltage commanded at a step of a sweep of step_count steps, 0 to full scale inclusive. The pixel itself sits
// above this by the shunt drop (see ina226.h)
#define PEROVSKITE_STEP_MV(step, full_scale_mv, step_count) ((uint32_t)(step) * (full_scale_mv) / ((step_count) - 1U))

/*
 * MRAM layout (little-endian). A sweep is measured into RAM, then saved as one MRAM record (see mram_map.h) in one of
 * PEROVSKITE_SLOT_COUNT slots of the perovskite region, overwriting the oldest sweep. A sweep cut short is never saved,
 * so its slot keeps the sweep it already had.
 */
#define PEROVSKITE_SLOT_COUNT 8U            // Newest sweeps kept in MRAM
#define PEROVSKITE_SLOT_SIZE 0xA00U         // Room for one record holding a perovskite_sweep_t
#define PEROVSKITE_SAMPLE_INVALID INT16_MIN // Step whose measurement failed; the INA226 never reports this value

typedef struct {
    uint32_t sequence;      // Counts up across reboots; the highest in MRAM is the newest sweep
    uint32_t timestamp;     // Unix time when the sweep started, see utc_get(); 0 if unavailable
    uint16_t full_scale_mv; // Input to PEROVSKITE_STEP_MV for this sweep
    uint16_t failed_steps;  // Number of steps whose measurement failed (PEROVSKITE_SAMPLE_INVALID) in this sweep
    uint16_t step_count;    // Sampling resolution: steps measured per pixel. current[][step_count..] are PEROVSKITE_SAMPLE_INVALID
    uint16_t reserved;      // 0
    // Pixel current in INA226_CURRENT_LSB_NA (250 nA) units, positive when the pixel sources current
    int16_t current[PEROVSKITE_PIXEL_COUNT][PEROVSKITE_STEP_COUNT];
} perovskite_sweep_t;

// Keep the TCB above the downward-growing stack, as with the other tasks.
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t perovskite_task_stack[PEROVSKITE_TASK_STACK_SIZE];
    uint8_t perovskite_command_queue_buffer[PEROVSKITE_QUEUE_LENGTH * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t perovskite_task_queue;
    StaticTask_t perovskite_task_tcb;
} perovskite_task_memory_t;

extern perovskite_task_memory_t perovskite_mem;

QueueHandle_t init_perovskite(void);
void main_perovskite(void *pvParameters);
command_t get_perovskite_sweep_command(void);
void exec_command_perovskite(command_t *const p_cmd);
void perovskite_checkin(void);

// Sets the sampling resolution (steps per pixel) of the following sweeps, clamped to PEROVSKITE_MIN_STEP_COUNT ..
// PEROVSKITE_STEP_COUNT. Set from the TAKE_IV_CURVE uplink once the pass ends. Kept in RAM only: back to
// PEROVSKITE_STEP_COUNT on reboot. Safe to call from any task
void perovskite_set_step_count(uint32_t step_count);

// Reads back the sweep saved in a slot. SUCCESS only if the slot holds a complete sweep that passes its CRC.
status_t perovskite_load_sweep(uint8_t slot, perovskite_sweep_t *p_sweep);

// Finds the sweep saved in a slot without loading it, for reading it straight out of MRAM (e.g. to downlink it): a
// perovskite_sweep_t at *p_offset within MRAM_REGION_PEROVSKITE. Unlike perovskite_load_sweep(), only checks the
// record's size, not its CRC.
status_t perovskite_sweep_mram_offset(uint8_t slot, uint32_t *p_offset);

// Finds the slot of the newest sweep in MRAM (the highest sequence number), checking records as
// perovskite_sweep_mram_offset() does. ERROR_READ_FAILED if no slot holds a sweep. Safe to call from any task
status_t perovskite_newest_sweep_slot(uint8_t *p_slot);

// The state machine can stop this task mid-sweep. Suspending it blindly could leave a pixel biased, or leave it
// holding the I2C bus shared with the fuel gauges, so it stops itself at a safe point (same handshake as the camera).
bool perovskite_stop_requested(void);
bool perovskite_prepare_suspend(void);
void perovskite_prepare_resume(void);
void perovskite_park_if_stopped(void);

#endif // PEROVSKITE_TASK_H
