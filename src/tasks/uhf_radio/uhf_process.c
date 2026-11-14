/**
 * uhf_process.c
 *
 * State handlers for the PVDX UHF Synchronization Protocol (PUSP), the half-duplex turn-taking protocol in pusp.h.
 * main_uhf_radio() only waits, polls for a frame and dispatches; everything a state does lives in its handler.
 *
 * Radio model:
 * - Radio state changes (TXPREP, TX, RX) are polled and take a few ms, so transmits block. uhf_transmit_pusp() always
 *   leaves the radio in RX. BEACON and ESTABLISH_SEND keep it in TXPREP between frames.
 * - DATASHEET(verify): after a frame is sent (TXFE) or received (RXFE) the radio goes to TXPREP by itself (AT86RF215
 *   datasheet 5.2.7, 5.3.2.4). Radio behaviour taken from the datasheet is marked DATASHEET(verify) throughout.
 * - The only radio interrupt used is RXFE. uhf_rx_poll() reads the frame and puts the radio back in RX.
 *
 * Link states (every state but BEACON and LISTEN) start with uhf_link_check(), which handles a restart SYN and the
 * link inactivity timeout.
 *
 * Retry rule: the side waiting for a reply to its own directive re-sends on timeout, through uhf_retry(). The other
 * side only re-sends its reply when it gets a duplicate, and then calls uhf_rearm_retry() so the retry timer doesn't
 * re-send again straight after.
 */
#include "logging.h"
#include "uhf_radio_task.h"

const uhf_state_desc_t uhf_state_descs[UHF_STATE_COUNT] = {
    [BEACON] = {.proc = beacon_proc, .retry_ms = 0, .max_retries = 0},
    [LISTEN] = {.proc = listen_proc, .retry_ms = 0, .max_retries = 0},
    [SYN_RECV] = {.proc = syn_recv_proc, .retry_ms = UHF_RETRY_MS, .max_retries = UHF_MAX_RETRIES},
    [ESTABLISH_RECV] = {.proc = establish_recv_proc, .retry_ms = 0, .max_retries = 0},
    [TRANS_TO_TX] = {.proc = transition_to_tx_proc, .retry_ms = UHF_RETRY_MS, .max_retries = UHF_MAX_RETRIES},
    [ESTABLISH_SEND] = {.proc = establish_send_proc, .retry_ms = 0, .max_retries = 0},
    [TRANS_TO_RX] = {.proc = transition_to_rx_proc, .retry_ms = UHF_RETRY_MS, .max_retries = UHF_MAX_RETRIES},
};

/**
 * Sends the beacon frame back to back until beacon_end (UHF_BEACON_BURST_MS after LISTEN started the burst), then goes
 * back to LISTEN. One frame per iteration, so the main loop still checks in with the watchdog during the burst. The radio
 * stays in TXPREP between frames, so a SYN sent during the burst is missed and the ground re-sends it.
 */
uhf_state_t beacon_proc(uhf_ctx_t *ctx, const uhf_rx_t *rx) {
    if (uhf_deadline_passed(ctx->beacon_end)) {
        uhf_go_rx();
        ctx->next_beacon = xTaskGetTickCount() + pdMS_TO_TICKS(UHF_BEACON_PERIOD_MS);
        return LISTEN;
    }

    // A failed frame is logged and the burst carries on: the next frame is another try
    uhf_transmit_beacon();
    return BEACON;
}

/**
 * No link. Radio in RX, waiting for a SYN.
 */
uhf_state_t listen_proc(uhf_ctx_t *ctx, const uhf_rx_t *rx) {
    switch (rx->kind) {
        case UHF_RX_CFDP:
            // Dropped: CFDP only runs over an established link, since replies to it can only go out in ESTABLISH_SEND and
            // would otherwise back up the CFDP -> UHF downlink queue
            warning("Radio: dropping CFDP PDU received in LISTEN, no link\n");
            return LISTEN;
        case UHF_RX_PUSP:
            if (rx->dir == SYN) {
                uhf_transmit_pusp(SYN_ACK);
                return SYN_RECV;
            }
            warning("Radio: unexpected PUSP directive %u in LISTEN\n", rx->dir);
            return LISTEN;
        case UHF_RX_NONE:
            break;
    }

    if (!uhf_deadline_passed(ctx->next_beacon)) {
        return LISTEN;
    }

    // Start a beacon burst. Telemetry is collected once per burst, not per frame
    if (!uhf_beacon_prepare()) {
        ctx->next_beacon = xTaskGetTickCount() + pdMS_TO_TICKS(UHF_BEACON_PERIOD_MS);
        return LISTEN;
    }
    ctx->beacon_end = xTaskGetTickCount() + pdMS_TO_TICKS(UHF_BEACON_BURST_MS);
    return BEACON;
}

/**
 * SYN_ACK sent, waiting for SRT. Re-sends SYN_ACK on timeout. A duplicate SYN is handled by uhf_link_check().
 */
uhf_state_t syn_recv_proc(uhf_ctx_t *ctx, const uhf_rx_t *rx) {
    const uhf_state_t link = uhf_link_check(ctx, rx);
    if (link != ctx->state) {
        return link;
    }

    if (uhf_rx_is_pusp(rx, SRT)) {
        return ESTABLISH_RECV;
    }
    if (rx->kind == UHF_RX_CFDP) {
        // SRT was lost but the ground thinks the link is up: treat this as the SRT
        uhf_forward_cfdp(rx);
        return ESTABLISH_RECV;
    }
    return uhf_retry(ctx, SYN_ACK);
}

/**
 * The ground has the turn. Radio in RX. No retry timer: only the link inactivity timeout gets us out of here without a
 * directive.
 */
uhf_state_t establish_recv_proc(uhf_ctx_t *ctx, const uhf_rx_t *rx) {
    const uhf_state_t link = uhf_link_check(ctx, rx);
    if (link != ctx->state) {
        return link;
    }

    if (rx->kind == UHF_RX_CFDP) {
        uhf_forward_cfdp(rx);
    } else if (uhf_rx_is_pusp(rx, YLD)) {
        uhf_transmit_pusp(YLD_ACK);
        return TRANS_TO_TX;
    } else if (uhf_rx_is_pusp(rx, YLD_ACK)) {
        // We came from TRANS_TO_RX and our SWC was lost. The ground is still waiting for it
        uhf_transmit_pusp(SWC);
    }
    // Anything else, including a duplicate SRT, is ignored
    return ESTABLISH_RECV;
}

/**
 * YLD_ACK sent, waiting for SWC. Re-sends YLD_ACK on timeout.
 */
uhf_state_t transition_to_tx_proc(uhf_ctx_t *ctx, const uhf_rx_t *rx) {
    const uhf_state_t link = uhf_link_check(ctx, rx);
    if (link != ctx->state) {
        return link;
    }

    if (uhf_rx_is_pusp(rx, SWC)) {
        return ESTABLISH_SEND; // uhf_enter_state() sets yield_deadline
    }
    if (uhf_rx_is_pusp(rx, YLD)) {
        // Our YLD_ACK was lost
        uhf_transmit_pusp(YLD_ACK);
        uhf_rearm_retry(ctx);
        return TRANS_TO_TX;
    }
    if (rx->kind == UHF_RX_CFDP) {
        // The ground shouldn't send data after YLD, but CFDP copes with duplicates
        uhf_forward_cfdp(rx);
    }
    return uhf_retry(ctx, YLD_ACK);
}

/**
 * We have the turn. The radio stays in TXPREP between frames, so nothing is received here. That's why yield_deadline
 * must come before the link timeout (see the static assert on UHF_TX_WINDOW_MS).
 */
uhf_state_t establish_send_proc(uhf_ctx_t *ctx, const uhf_rx_t *rx) {
    const uhf_state_t link = uhf_link_check(ctx, rx);
    if (link != ctx->state) {
        return link;
    }

    uhf_go_txprep(); // Polled. DATASHEET(verify): assumes CMD=TXPREP is harmless when already in TXPREP

    bool more = uxQueueMessagesWaiting(downlink_queue) != 0;
    while (more && !uhf_deadline_passed(ctx->yield_deadline)) {
        more = uhf_tx_process(); // TODO: must block until the frame is out, polling TXFE like uhf_transmit_beacon()
        ctx->grace_armed = false;
        uhf_watchdog_checkin();
    }

    // TEMP DECISION: with time left in the window, wait up to UHF_YIELD_GRACE_MS for more data before yielding, so we
    // don't hand over turns with nothing in them. The main loop wakes on UHF_EVENT_DOWNLINK or the grace deadline
    if (!uhf_deadline_passed(ctx->yield_deadline)) {
        if (!ctx->grace_armed) {
            ctx->grace_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(UHF_YIELD_GRACE_MS);
            ctx->grace_armed = true;
            return ESTABLISH_SEND;
        }
        if (!uhf_deadline_passed(ctx->grace_deadline)) {
            return ESTABLISH_SEND;
        }
    }

    uhf_transmit_pusp(YLD); // Leaves the radio in RX
    return TRANS_TO_RX;
}

/**
 * YLD sent, waiting for YLD_ACK. Re-sends YLD on timeout. SWC has no ack, so once it's sent we go straight to
 * ESTABLISH_RECV, which re-sends SWC if a duplicate YLD_ACK shows it was lost.
 */
uhf_state_t transition_to_rx_proc(uhf_ctx_t *ctx, const uhf_rx_t *rx) {
    const uhf_state_t link = uhf_link_check(ctx, rx);
    if (link != ctx->state) {
        return link;
    }

    if (uhf_rx_is_pusp(rx, YLD_ACK)) {
        uhf_transmit_pusp(SWC);
        return ESTABLISH_RECV;
    }
    if (rx->kind == UHF_RX_CFDP) {
        // The ground has already taken the turn: treat this as the end of the handover
        uhf_forward_cfdp(rx);
        return ESTABLISH_RECV;
    }
    return uhf_retry(ctx, YLD);
}
