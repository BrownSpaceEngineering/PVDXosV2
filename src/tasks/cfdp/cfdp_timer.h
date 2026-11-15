#ifndef CFDP_TIMER_H
#define CFDP_TIMER_H

#include "ccsds/radio_routing.h"
#include "cfdp_task.h"

// Backstop only: a transaction also ends with its pass, or after the retransmit limits
#define TRANSACTION_LIFETIME_MS 600000UL // 10 min

// In our turns: how many times an EOF/FIN or NAK is re-sent without an answer before giving up
#define ACK_RETRANSMIT_LIMIT 16
#define NAK_RETRANSMIT_LIMIT 16

#define CFDP_TIMER_TICKS_TO_WAIT 10 // placeholders

void init_cfdp_timers(cfdp_task_memory_t *mem);

static inline void reset_timer(TimerHandle_t timer_handle) {
    info("Reseting Timer\n");
    xTimerReset(timer_handle, CFDP_TIMER_TICKS_TO_WAIT);
}

static inline void stop_timer(TimerHandle_t timer_handle) {
    info("Stoping Timer\n");
    xTimerStop(timer_handle, CFDP_TIMER_TICKS_TO_WAIT);
}

static inline void start_timer(TimerHandle_t timer_handle) {
    info("Starting Timer\n");
    xTimerStart(timer_handle, CFDP_TIMER_TICKS_TO_WAIT);
}

// Retransmit at the start of the first of our turns by which the ground has had a full turn to answer
static inline void arm_retransmit(cfdp_transaction_t *txn) {
    txn->retransmit_armed = true;
    txn->retransmit_turn = radio_retransmit_turn();
}

static inline void disarm_retransmit(cfdp_transaction_t *txn) {
    txn->retransmit_armed = false;
}

void inactivity_timer_timeout(void);
void cfdp_turn_retransmit(void);

void inactivity_timer_callback(TimerHandle_t timer);

#endif
