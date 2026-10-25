/**
 * src/telemetry/telemetry_timer.h
 *
 * Periodic timer that asks CFDP to downlink telemetry
 *
 * Created: 20261023 FRI
 * Updated: 20261023 FRI
 * Authors: Noah Shepard
 */

#ifndef PVDX_TELEMETRY_TIMER_H
#define PVDX_TELEMETRY_TIMER_H

// How often telemetry is downlinked
#define TELEMETRY_PERIOD_MS 600000UL // ten minutes

/**
 * Creates the telemetry timer. Must be called once, before start_telemetry_timer()
 */
void init_telemetry_timer(void);

/**
 * Starts the telemetry timer. Every TELEMETRY_PERIOD_MS after this, an OPERATION_CFDP_PUT_TELEMETRY command is sent
 * to the CFDP task
 */
void start_telemetry_timer(void);

#endif // PVDX_TELEMETRY_TIMER_H
