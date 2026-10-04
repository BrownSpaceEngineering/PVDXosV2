#ifndef AT86_TEST_TASK_H
#define AT86_TEST_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>

#include "drivers/at86rf215/at86rf215.h"
#include "globals.h"

// Memory for the AT86RF215 test task
#define AT86_TEST_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t at86_test_task_stack[AT86_TEST_TASK_STACK_SIZE];
    StaticTask_t at86_test_task_tcb;
} at86_test_task_memory_t;

// Snapshot of the registers of one RF front end (RF09 or RF24) and its baseband core (BBC0 or BBC1).
typedef struct {
    uint8_t irqm;
    uint8_t auxs;
    uint8_t state;
    uint8_t cmd;
    uint8_t cs;
    uint8_t ccf0l;
    uint8_t ccf0h;
    uint8_t cnl;
    uint8_t cnm;
    uint8_t rxbwc;
    uint8_t agcc;
    uint8_t agcs;
    uint8_t rssi;
    uint8_t txcutc;
    uint8_t pac;
    uint8_t padfe;
    uint8_t pll;
    uint8_t bbc_irqm;
    uint8_t bbc_pc;
} at86_radio_regs_t;

// Mailbox operations, set from GDB with `set var at86_test.mbox.op = ...`
typedef enum {
    AT86_MBOX_IDLE = 0,
    AT86_MBOX_READ8 = 1,  // value <- register[addr]
    AT86_MBOX_WRITE8 = 2, // register[addr] <- (uint8_t)value
    AT86_MBOX_READ32 = 3, // value <- register[addr..addr+3] (MS byte first)
} at86_mbox_op_t;

// GDB mailbox: set addr (and value for writes), then set op last. The task performs the access on its next
// loop, stores the driver return code in ret, increments count and sets op back to AT86_MBOX_IDLE.
typedef struct {
    uint32_t op;
    uint16_t addr;
    uint32_t value;
    int32_t ret;
    uint32_t count;
} at86_mailbox_t;

typedef struct {
    int32_t init_ret;    // Return code of at86rf215_init()
    int32_t conn_ret;    // Return code of at86rf215_conn_check()
    int32_t last_ret;    // First nonzero return code of the most recent register dump (0 if all reads succeeded)
    uint32_t loop_count; // Number of completed register dumps

    // Common registers
    uint8_t rst;
    uint8_t cfg;
    uint8_t clko;
    uint8_t bmdvc;
    uint8_t xoc;
    uint8_t iqifc0;
    uint8_t iqifc1;
    uint8_t iqifc2;
    uint8_t pn;
    uint8_t vn;

    at86_radio_regs_t radio[2]; // [AT86RF215_RF09] = RF09/BBC0, [AT86RF215_RF24] = RF24/BBC1

    at86_mailbox_t mbox;
} at86_test_state_t;

extern at86_test_task_memory_t at86_test_mem;
extern struct at86rf215 at86_dev;
extern volatile at86_test_state_t at86_test;

void main_at86_test(void *pvParameters);

#endif // AT86_TEST_TASK_H
