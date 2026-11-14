/**
 * uhf_state.c
 *
 * State machine helpers for the UHF radio task: state entry, deadlines, retries and the checks shared by every link
 * state. The state handlers themselves are in uhf_process.c.
 */
#include "logging.h"
#include "uhf_radio_task.h"

static const char *const uhf_state_names[UHF_STATE_COUNT] = {
    [BEACON] = "BEACON",           [LISTEN] = "LISTEN",
    [SYN_RECV] = "SYN_RECV",       [ESTABLISH_RECV] = "ESTABLISH_RECV",
    [TRANS_TO_TX] = "TRANS_TO_TX", [ESTABLISH_SEND] = "ESTABLISH_SEND",
    [TRANS_TO_RX] = "TRANS_TO_RX",
};

bool uhf_deadline_passed(TickType_t deadline) {
    return (int32_t)(xTaskGetTickCount() - deadline) >= 0;
}

void uhf_enter_state(uhf_ctx_t *ctx, uhf_state_t next) {
    info("Radio: %s -> %s\n", uhf_state_names[ctx->state], uhf_state_names[next]);

    ctx->state = next;
    ctx->retries = 0;
    ctx->grace_armed = false;
    uhf_rearm_retry(ctx);
    if (next == ESTABLISH_SEND) {
        ctx->yield_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(UHF_TX_WINDOW_MS);
    }
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
        info("Radio: SYN in %s, the ground restarted the link\n", uhf_state_names[ctx->state]);
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
static TickType_t uhf_ticks_until(TickType_t now, TickType_t deadline) {
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
        TickType_t left = uhf_ticks_until(now, ctx->yield_deadline);
        if (ctx->grace_armed) {
            const TickType_t grace_left = uhf_ticks_until(now, ctx->grace_deadline);
            left = grace_left < left ? grace_left : left;
        }
        wait = left < wait ? left : wait;
    }

    return wait;
}
