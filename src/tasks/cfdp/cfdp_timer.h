#ifndef CFDP_TIMER_H
#define CFDP_TIMER_H

#include "cfdp_task.h"

// TODO(temp): placeholder timer lengths. A ~540 byte UHF frame alone takes ~0.7 s at 6.25 kb/s, so these must be set
// from the real air time plus the ground round trip before flight
#define RETRANSMIT_TIMEOUT_MS 100UL
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

void inactivity_timer_timeout(const cfdp_timeout_t *timeout);
void retransmit_timer_timeout(const cfdp_timeout_t *timeout);

void inactivity_timer_callback(TimerHandle_t timer);
void retransmit_timer_callback(TimerHandle_t timer);

#endif
