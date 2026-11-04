#ifndef PEROVSKITE_TASK_H
#define PEROVSKITE_TASK_H

#include "drivers/pmb/adg734.h"
#include "globals.h"

#define PEROVSKITE_TASK_STACK_SIZE 512 // Stack size in words
#define PEROVSKITE_QUEUE_LENGTH 4      // The only command is one sweep request per state entry

#define PEROVSKITE_PIXEL_COUNT PMB_PIXEL_COUNT
#define PEROVSKITE_STEP_COUNT 64U // Voltage steps per pixel, 0 to full scale inclusive

// FOLLOW voltage commanded at a step. The pixel itself sits above this by the shunt drop (see ina226.h)
#define PEROVSKITE_STEP_MV(step, full_scale_mv) ((uint32_t)(step) * (full_scale_mv) / (PEROVSKITE_STEP_COUNT - 1U))

/*
 * MRAM layout (little-endian). A sweep is too big to hold in RAM, so it goes to MRAM as it is measured, one page at a
 * time. Each of the PEROVSKITE_RECORD_COUNT records is PEROVSKITE_DATA_PAGE_COUNT data pages followed by a header page.
 * The header is erased first and written last, so a record only has a valid header once its whole sweep is saved.
 */
#define PEROVSKITE_MRAM_PAGE_SIZE 256U      // mram_read_bytes() only reads whole pages
#define PEROVSKITE_PIXELS_PER_PAGE 2U       // 2 pixels * 64 steps * 2 bytes
#define PEROVSKITE_DATA_PAGE_COUNT (PEROVSKITE_PIXEL_COUNT / PEROVSKITE_PIXELS_PER_PAGE)
#define PEROVSKITE_RECORD_SIZE ((PEROVSKITE_DATA_PAGE_COUNT + 1U) * PEROVSKITE_MRAM_PAGE_SIZE)
#define PEROVSKITE_RECORD_COUNT 8U          // Newest sweeps kept in MRAM; the oldest is overwritten
#define PEROVSKITE_SWEEP_MAGIC 0x4B535650U  // Change if the layout below changes
#define PEROVSKITE_SAMPLE_INVALID INT16_MIN // Step whose measurement failed; the INA226 never reports this value

typedef struct {
    // Pixel current in INA226_CURRENT_LSB_NA (250 nA) units, positive when the pixel sources current.
    // Data page N holds pixels 2N and 2N + 1
    int16_t current[PEROVSKITE_PIXELS_PER_PAGE][PEROVSKITE_STEP_COUNT];
} perovskite_data_page_t;

typedef struct {
    uint32_t crc;           // crc32() of everything after this field
    uint32_t magic;         // PEROVSKITE_SWEEP_MAGIC
    uint32_t sequence;      // Counts up across reboots; the highest in MRAM is the newest sweep
    uint32_t timestamp;     // Unix time when the sweep started, see adcs_get_time(); 0 if unavailable
    uint16_t full_scale_mv; // Input to PEROVSKITE_STEP_MV for this sweep
    uint16_t failed_steps;  // Number of PEROVSKITE_SAMPLE_INVALID samples in this sweep
    uint32_t page_crc[PEROVSKITE_DATA_PAGE_COUNT]; // crc32() of each data page
    uint8_t reserved[PEROVSKITE_MRAM_PAGE_SIZE - 20U - 4U * PEROVSKITE_DATA_PAGE_COUNT]; // Zeros
} perovskite_header_t;

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

// Perovskite task only (the MRAM driver has no lock). Read back a saved sweep, one page at a time.
// SUCCESS only if the record holds a complete sweep / the page matches its header's CRC.
status_t perovskite_load_header(uint8_t record, perovskite_header_t *p_header);
status_t perovskite_load_page(uint8_t record, uint8_t page, const perovskite_header_t *p_header, perovskite_data_page_t *p_page);

// The state machine can stop this task mid-sweep. Suspending it blindly could leave a pixel biased, or leave it
// holding the I2C bus shared with the fuel gauges, so it stops itself at a safe point (same handshake as the camera).
bool perovskite_stop_requested(void);
bool perovskite_prepare_suspend(void);
void perovskite_prepare_resume(void);
void perovskite_park_if_stopped(void);

#endif // PEROVSKITE_TASK_H
