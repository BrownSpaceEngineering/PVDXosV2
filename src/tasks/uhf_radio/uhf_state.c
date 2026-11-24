/**
 * uhf_state.c
 *
 * State machine helpers for the UHF radio task: state entry, deadlines, retries and the checks shared by every link
 * state. The state handlers themselves are in uhf_process.c.
 */
#include "ccsds/radio_routing.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "state_machine.h"
#include "uhf_radio_task.h"

// The state machine inputs this task owns (static: the task manager reads them after the command is sent)
static input_update_data_t uhf_pass_inputs = {
    .input_update_bitmask = INPUT_UPDATE_BIT(UHF_PASS_UPDATE) | INPUT_UPDATE_BIT(SBAND_PASS_UPDATE),
};

// Only logged, and release builds compile logging out
[[maybe_unused]] static const char *const uhf_state_names[UHF_STATE_COUNT] = {
    [BEACON] = "BEACON",           [LISTEN] = "LISTEN",
    [SYN_RECV] = "SYN_RECV",       [ESTABLISH_RECV] = "ESTABLISH_RECV",
    [TRANS_TO_TX] = "TRANS_TO_TX", [ESTABLISH_SEND] = "ESTABLISH_SEND",
    [TRANS_TO_RX] = "TRANS_TO_RX",
};

/**
 * Starts or ends a pass: tells CFDP (radio_link_set_up()) and the state machine, which goes to the pass state of the
 * pass's type. A pass starting while one is up is a new pass (the type changed)
 */
static void uhf_set_pass(const uhf_ctx_t *ctx, bool up) {
    radio_link_set_up(up, ctx->sband_pass);

    uhf_pass_inputs.new_values.uhf_pass_pending = up && !ctx->sband_pass;
    uhf_pass_inputs.new_values.sband_pass_pending = up && ctx->sband_pass;
    // The PUSP tests change states before the scheduler (and the task manager) is running
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return;
    }
    command_t cmd = get_state_input_update_command(&uhf_pass_inputs);
    const status_t status = enqueue_command(&cmd);
    if (status != SUCCESS) {
        warning("Radio: failed to report the pass to the state machine (status %d)\n", status);
    }
}

bool uhf_deadline_passed(TickType_t deadline) {
    return (int32_t)(xTaskGetTickCount() - deadline) >= 0;
}

// BEACON and LISTEN have no link. Every other state is part of a pass
static bool uhf_is_link_state(uhf_state_t state) {
    return state != BEACON && state != LISTEN;
}

void uhf_enter_state(uhf_ctx_t *ctx, uhf_state_t next) {
    info("Radio: %s -> %s\n", uhf_state_names[ctx->state], uhf_state_names[next]);

    // Tell CFDP about passes and our turns. A SYN restarting the link mid-pass (-> SYN_RECV) stays in the same pass
    if (ctx->state == ESTABLISH_SEND) {
        radio_turn_ended();
    }
    if (uhf_is_link_state(next) != uhf_is_link_state(ctx->state)) {
        uhf_set_pass(ctx, uhf_is_link_state(next));
    }
    if (next == ESTABLISH_SEND) {
        radio_turn_started();
    }

    ctx->state = next;
    ctx->retries = 0;
    uhf_rearm_retry(ctx);
    if (next == ESTABLISH_SEND) {
        ctx->yield_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(UHF_TX_WINDOW_MS);
    }
}

uhf_state_t uhf_step(uhf_ctx_t *ctx, const uhf_rx_t *rx) {
    if (rx->kind == UHF_RX_PUSP) {
        dispatch_pusp_immediate_cmd(rx->notifs);
    }

    if (!uhf_tx_is_enabled() && ctx->state != LISTEN) {
        // BEACON and ESTABLISH_SEND keep the radio in TXPREP; every other state is already in RX. The frame that got us
        // here belonged to the link being dropped, so LISTEN's handler doesn't see it
        if (ctx->state == BEACON || ctx->state == ESTABLISH_SEND) {
            uhf_go_rx();
        }
        uhf_enter_state(ctx, LISTEN);
        return LISTEN;
    }

    const uhf_state_t next = uhf_state_descs[ctx->state].proc(ctx, rx);
    if (next != ctx->state) {
        uhf_enter_state(ctx, next);
    }
    return ctx->state;
}

void uhf_rearm_retry(uhf_ctx_t *ctx) {
    const uint16_t retry_ms = uhf_state_descs[ctx->state].retry_ms;
    if (retry_ms != 0) {
        ctx->retry_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(retry_ms);
    }
}

uhf_state_t uhf_retry(uhf_ctx_t *ctx, pusp_directive_t dir) {
    const uhf_state_desc_t *desc = &uhf_state_descs[ctx->state];
    if (desc->retry_ms == 0 || !uhf_deadline_passed(ctx->retry_deadline)) {
        return ctx->state;
    }

    if (ctx->retries >= desc->max_retries) {
        warning("Radio: no reply in %s after %u re-sends, back to LISTEN\n", uhf_state_names[ctx->state], ctx->retries);
        return LISTEN;
    }

    ctx->retries++;
    debug("Radio: %s timed out, re-sending directive %u (%u/%u)\n", uhf_state_names[ctx->state], dir, ctx->retries, desc->max_retries);
    uhf_transmit_pusp(dir);
    uhf_rearm_retry(ctx);
    return ctx->state;
}

uhf_state_t uhf_link_check(uhf_ctx_t *ctx, const uhf_rx_t *rx) {
    if (uhf_rx_is_pusp(rx, SYN)) {
        const bool sband = uhf_rx_sband_pass(rx);
        if (sband != ctx->sband_pass) {
            info("Radio: SYN in %s changes the pass to %s, starting a new pass\n", uhf_state_names[ctx->state],
                 sband ? "UHF + S-band" : "UHF");
            uhf_set_pass(ctx, false);
            ctx->sband_pass = sband;
            uhf_set_pass(ctx, true);
        } else {
            info("Radio: SYN in %s, the ground restarted the link\n", uhf_state_names[ctx->state]);
        }
        uhf_transmit_pusp(SYN_ACK);
        if (ctx->state == SYN_RECV) {
            uhf_rearm_retry(ctx); // Staying in SYN_RECV, so uhf_enter_state() won't re-arm it
        }
        return SYN_RECV;
    }

    if (xTaskGetTickCount() - ctx->last_rx > pdMS_TO_TICKS(UHF_LINK_TIMEOUT_MS)) {
        warning("Radio: nothing from the ground for %d ms in %s, dropping the link\n", UHF_LINK_TIMEOUT_MS, uhf_state_names[ctx->state]);
        return LISTEN;
    }

    return ctx->state;
}

// Ticks from now until deadline, 0 if it has passed
TickType_t uhf_ticks_until(TickType_t now, TickType_t deadline) {
    const int32_t left = (int32_t)(deadline - now);
    return left > 0 ? (TickType_t)left : 0;
}

TickType_t uhf_ticks_until_wake(const uhf_ctx_t *ctx) {
    // BEACON sends its frames back to back
    if (ctx->state == BEACON) {
        return 0;
    }

    const TickType_t now = xTaskGetTickCount();
    // The cap keeps watchdog check-ins on time. The link inactivity timeout isn't a wake-up of its own: being noticed
    // up to UHF_RADIO_WAIT_MS late doesn't matter
    TickType_t wait = pdMS_TO_TICKS(UHF_RADIO_WAIT_MS);

    if (uhf_state_descs[ctx->state].retry_ms != 0) {
        const TickType_t left = uhf_ticks_until(now, ctx->retry_deadline);
        wait = left < wait ? left : wait;
    }

    if (ctx->state == LISTEN) {
        const TickType_t left = uhf_ticks_until(now, ctx->next_beacon);
        wait = left < wait ? left : wait;
    } else if (ctx->state == ESTABLISH_SEND) {
        const TickType_t left = uhf_ticks_until(now, ctx->yield_deadline);
        wait = left < wait ? left : wait;
    }

    return wait;
}
