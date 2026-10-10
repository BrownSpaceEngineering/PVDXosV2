#ifndef CFDP_TIMER_H
#define CFDP_TIMER_H

#include "cfdp_task.h"

#define RETRANSMIT_TIMEOUT_MS 100UL // placeholders
#define TRANSACTION_LIFETIME_MS 20000UL

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

void inactivity_timer_timeout(cfdp_transaction_t *txn);
void retransmit_timer_timeout(cfdp_transaction_t *txn);

void inactivity_timer_callback(TimerHandle_t timer);
void retransmit_timer_callback(TimerHandle_t timer);

#endif
