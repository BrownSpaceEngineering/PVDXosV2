/**
 * test_pusp.c
 *
 * Tests of the UHF task's PUSP state machine: the handlers in uhf_process.c and the helpers in uhf_state.c, driven
 * with made-up received frames. Directives are "sent" through uhf_transmit_pusp(), which only records them, since the
 * radio isn't set up yet when tests run.
 *
 * Tests run before the scheduler starts, so the tick count stays put: a deadline is expired by setting it to now.
 * BEACON and ESTABLISH_SEND's handlers, and LISTEN starting a beacon, use the radio and aren't tested here.
 */

#include "tests/test_pusp.h"

#include "logging.h"
#include "tests/test.h"
#include "uhf_radio/uhf_radio_task.h"

#if defined(UNITTEST) && defined(TEST_PUSP)

static const uhf_rx_t RX_NONE = {.kind = UHF_RX_NONE};
static const uhf_rx_t RX_CFDP = {.kind = UHF_RX_CFDP, .item = NULL}; // NULL item: uhf_forward_cfdp() forwards nothing

static uhf_rx_t pusp_rx(pusp_directive_t dir) {
    return (uhf_rx_t){.kind = UHF_RX_PUSP, .dir = dir};
}

// A ctx entered into state the normal way, just heard from the ground, with no beacon due. Clears the sent count
static uhf_ctx_t pusp_ctx(uhf_state_t state) {
    const TickType_t now = xTaskGetTickCount();
    uhf_ctx_t ctx = {.state = LISTEN, .last_rx = now, .next_beacon = now + pdMS_TO_TICKS(UHF_BEACON_PERIOD_MS)};
    uhf_enter_state(&ctx, state);
    uhf_pusp_tx_count = 0;
    return ctx;
}

// One main loop iteration: runs the state's handler on rx and enters the state it returns
static uhf_state_t pusp_step(uhf_ctx_t *ctx, uhf_rx_t rx) {
    const uhf_state_t next = uhf_state_descs[ctx->state].proc(ctx, &rx);
    if (next != ctx->state) {
        uhf_enter_state(ctx, next);
    }
    return next;
}

// True if exactly one directive, dir, was sent since the count was last cleared
static bool pusp_sent(pusp_directive_t dir) {
    return uhf_pusp_tx_count == 1 && uhf_pusp_tx_last == dir;
}

static void test_pusp_timing(void) {
    const TickType_t now = xTaskGetTickCount();
    PVDX_ASSERT_MSG(uhf_deadline_passed(now) && uhf_deadline_passed(now - 10), "deadline: now and past have passed\n");
    PVDX_ASSERT_MSG(!uhf_deadline_passed(now + 10), "deadline: future hasn't passed\n");

    uhf_ctx_t ctx = pusp_ctx(SYN_RECV);
    ctx.retries = 3;
    uhf_enter_state(&ctx, TRANS_TO_TX);
    PVDX_ASSERT_MSG(ctx.state == TRANS_TO_TX && ctx.retries == 0, "enter: resets the state's counters\n");
    PVDX_ASSERT_MSG(ctx.retry_deadline == now + pdMS_TO_TICKS(UHF_RETRY_MS), "enter: arms the retry timer\n");
    uhf_enter_state(&ctx, ESTABLISH_SEND);
    PVDX_ASSERT_MSG(ctx.yield_deadline == now + pdMS_TO_TICKS(UHF_TX_WINDOW_MS), "enter: sets the yield deadline\n");

    ctx = pusp_ctx(BEACON);
    PVDX_ASSERT_MSG(uhf_ticks_until_wake(&ctx) == 0, "wake: BEACON doesn't wait\n");
    ctx = pusp_ctx(LISTEN);
    PVDX_ASSERT_MSG(uhf_ticks_until_wake(&ctx) == pdMS_TO_TICKS(UHF_RADIO_WAIT_MS), "wake: capped by UHF_RADIO_WAIT_MS\n");
    ctx = pusp_ctx(SYN_RECV);
    ctx.retry_deadline = now + 10;
    PVDX_ASSERT_MSG(uhf_ticks_until_wake(&ctx) == 10, "wake: at the retry deadline\n");
    ctx.retry_deadline = now;
    PVDX_ASSERT_MSG(uhf_ticks_until_wake(&ctx) == 0, "wake: retry deadline passed\n");
}

// A whole link: the ground opens it, has the turn, hands it to us, and we hand it back
static void test_pusp_handshake(void) {
    uhf_ctx_t ctx = pusp_ctx(LISTEN);
    PVDX_ASSERT_MSG(pusp_step(&ctx, pusp_rx(SYN)) == SYN_RECV && pusp_sent(SYN_ACK), "LISTEN: SYN -> SYN_ACK, SYN_RECV\n");

    uhf_pusp_tx_count = 0;
    PVDX_ASSERT_MSG(pusp_step(&ctx, pusp_rx(SRT)) == ESTABLISH_RECV && uhf_pusp_tx_count == 0, "SYN_RECV: SRT -> ESTABLISH_RECV\n");
    PVDX_ASSERT_MSG(pusp_step(&ctx, pusp_rx(YLD)) == TRANS_TO_TX && pusp_sent(YLD_ACK), "ESTABLISH_RECV: YLD -> YLD_ACK, TRANS_TO_TX\n");

    uhf_pusp_tx_count = 0;
    PVDX_ASSERT_MSG(pusp_step(&ctx, pusp_rx(SWC)) == ESTABLISH_SEND && uhf_pusp_tx_count == 0, "TRANS_TO_TX: SWC -> ESTABLISH_SEND\n");

    // ESTABLISH_SEND's handler uses the radio, so pick up after it has sent YLD
    ctx = pusp_ctx(TRANS_TO_RX);
    PVDX_ASSERT_MSG(pusp_step(&ctx, pusp_rx(YLD_ACK)) == ESTABLISH_RECV && pusp_sent(SWC), "TRANS_TO_RX: YLD_ACK -> SWC, ESTABLISH_RECV\n");
}

// Lost or duplicated directives, and packets that come in a state that isn't expecting them
static void test_pusp_recovery(void) {
    uhf_ctx_t ctx = pusp_ctx(LISTEN);
    PVDX_ASSERT_MSG(pusp_step(&ctx, RX_CFDP) == LISTEN && uhf_pusp_tx_count == 0, "LISTEN: CFDP dropped without a link\n");
    PVDX_ASSERT_MSG(pusp_step(&ctx, pusp_rx(SRT)) == LISTEN && uhf_pusp_tx_count == 0, "LISTEN: stray directive ignored\n");

    ctx = pusp_ctx(SYN_RECV);
    PVDX_ASSERT_MSG(pusp_step(&ctx, RX_CFDP) == ESTABLISH_RECV, "SYN_RECV: CFDP stands in for a lost SRT\n");

    ctx = pusp_ctx(SYN_RECV);
    ctx.retry_deadline = xTaskGetTickCount();
    PVDX_ASSERT_MSG(pusp_step(&ctx, pusp_rx(SYN)) == SYN_RECV && pusp_sent(SYN_ACK), "SYN_RECV: duplicate SYN -> SYN_ACK again\n");
    PVDX_ASSERT_MSG(!uhf_deadline_passed(ctx.retry_deadline), "SYN_RECV: duplicate SYN re-arms the retry timer\n");

    ctx = pusp_ctx(ESTABLISH_RECV);
    PVDX_ASSERT_MSG(pusp_step(&ctx, pusp_rx(YLD_ACK)) == ESTABLISH_RECV && pusp_sent(SWC),
                    "ESTABLISH_RECV: duplicate YLD_ACK -> SWC again\n");
    uhf_pusp_tx_count = 0;
    PVDX_ASSERT_MSG(pusp_step(&ctx, pusp_rx(SRT)) == ESTABLISH_RECV && uhf_pusp_tx_count == 0, "ESTABLISH_RECV: duplicate SRT ignored\n");
    PVDX_ASSERT_MSG(pusp_step(&ctx, pusp_rx(SYN)) == SYN_RECV && pusp_sent(SYN_ACK), "ESTABLISH_RECV: SYN restarts the link\n");

    ctx = pusp_ctx(TRANS_TO_TX);
    PVDX_ASSERT_MSG(pusp_step(&ctx, pusp_rx(YLD)) == TRANS_TO_TX && pusp_sent(YLD_ACK), "TRANS_TO_TX: duplicate YLD -> YLD_ACK again\n");

    ctx = pusp_ctx(TRANS_TO_RX);
    PVDX_ASSERT_MSG(pusp_step(&ctx, RX_CFDP) == ESTABLISH_RECV, "TRANS_TO_RX: CFDP means the ground took the turn\n");
}

// Each state waiting for a reply re-sends on timeout and gives up to LISTEN after UHF_MAX_RETRIES re-sends
static void test_pusp_retries(void) {
    static const struct {
        uhf_state_t state;
        pusp_directive_t resend;
    } waiting[] = {{SYN_RECV, SYN_ACK}, {TRANS_TO_TX, YLD_ACK}, {TRANS_TO_RX, YLD}};

    for (size_t i = 0; i < sizeof(waiting) / sizeof(waiting[0]); i++) {
        uhf_ctx_t ctx = pusp_ctx(waiting[i].state);
        PVDX_ASSERT_MSG(pusp_step(&ctx, RX_NONE) == waiting[i].state && uhf_pusp_tx_count == 0,
                        "retry: nothing sent before the deadline\n");

        ctx.retry_deadline = xTaskGetTickCount();
        PVDX_ASSERT_MSG(pusp_step(&ctx, RX_NONE) == waiting[i].state && pusp_sent(waiting[i].resend) && ctx.retries == 1,
                        "retry: re-sent on timeout\n");

        ctx.retries = UHF_MAX_RETRIES;
        ctx.retry_deadline = xTaskGetTickCount();
        PVDX_ASSERT_MSG(pusp_step(&ctx, RX_NONE) == LISTEN, "retry: gives up to LISTEN\n");
    }
}

// A link state drops back to LISTEN once nothing has come from the ground for UHF_LINK_TIMEOUT_MS
static void test_pusp_inactivity(void) {
    const TickType_t timeout = pdMS_TO_TICKS(UHF_LINK_TIMEOUT_MS);

    uhf_ctx_t ctx = pusp_ctx(ESTABLISH_RECV);
    ctx.last_rx = xTaskGetTickCount() - timeout;
    PVDX_ASSERT_MSG(pusp_step(&ctx, RX_NONE) == ESTABLISH_RECV, "inactivity: link kept at the timeout\n");
    ctx.last_rx = xTaskGetTickCount() - timeout - 1;
    PVDX_ASSERT_MSG(pusp_step(&ctx, RX_NONE) == LISTEN, "inactivity: link dropped after the timeout\n");
}

void test_pusp(void) {
    test_log("----- testing pusp -----\n");
    test_pusp_timing();
    test_pusp_handshake();
    test_pusp_recovery();
    test_pusp_retries();
    test_pusp_inactivity();
}

#endif // UNITTEST && TEST_PUSP
