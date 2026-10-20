#ifndef TEST_AT86_LINK_H
#define TEST_AT86_LINK_H

#include <stdint.h>

// Value of at86_link_status.magic once the test started. scripts/at86_selftest.py checks it over GDB
#define AT86_LINK_STATUS_MAGIC 0xA7860215u

typedef enum {
    LINK_PHASE_INIT = 0,
    LINK_PHASE_A_TX_DATA = 1,    // AT86 sends DATA
    LINK_PHASE_A_WAIT_ACK = 2,   // AT86 listens for the HackRF ACK
    LINK_PHASE_B_WAIT_DATA = 3,  // AT86 listens for HackRF DATA
    LINK_PHASE_B_TX_ACK = 4,     // AT86 sends ACK
    LINK_PHASE_B_DUP_WINDOW = 5, // AT86 listens for repeats of the same DATA and ACKs them again
    LINK_PHASE_DONE = 6,
    LINK_PHASE_INIT_FAILED = 7,
} link_phase_t;

/**
 * Live counters of the link test. The struct is global so that a debugger can read it while the test runs
 * (tools/gdb.sh "print at86_link_status"). Every field only grows, except the "last_" and state fields.
 */
typedef struct {
    uint32_t magic;
    uint32_t heartbeats;  // Heartbeat lines printed
    uint32_t polls;       // Iterations of every wait loop. Grows as long as the test code runs
    uint32_t uptime_ms;   // Time since the test started (DWT cycle counter)
    uint32_t phase;       // link_phase_t
    uint32_t round;       // Current round, starting at 0
    uint32_t spi_checks;  // Part number reads done by the heartbeat
    uint32_t spi_errors;  // Part number reads that failed or returned the wrong value
    uint32_t fe_errors;   // Heartbeats where PADFE is not configuration 1 or AUXS.EXTLNABYP is set
    uint32_t tx_frames;   // Frames that finished (TXFE)
    uint32_t tx_errors;   // Frames that timed out or failed on SPI
    uint32_t rx_listens;  // Times the radio entered RX
    uint32_t rx_frames;   // RXFE seen
    uint32_t rx_fcs_bad;  // RXFE with BBC0_PC.FCSOK = 0
    uint32_t rx_rejected; // Good FCS but not a valid packet for us
    uint32_t rx_timeouts; // Listen windows that ended with no frame
    uint32_t acks_rx;     // Valid ACKs for our DATA
    uint32_t acks_tx;     // ACKs sent (including repeats)
    uint32_t data_rx;     // New DATA packets from the HackRF
    uint32_t data_dup_rx; // Repeated DATA packets from the HackRF
    uint32_t rounds_ok;
    uint32_t rounds_fail;
    int32_t last_error;
    uint8_t part_number;
    uint8_t version;
    uint8_t rf_state; // Last RF09_STATE read
    uint8_t padfe;    // Last RF09_PADFE read
    uint8_t auxs;     // Last RF09_AUXS read
    uint8_t pll_locked;
    int8_t last_rssi; // dBm, not corrected for the external LNA
    int8_t last_edv;  // dBm, energy of the last frame
} at86_link_status_t;

extern volatile at86_link_status_t at86_link_status;

void test_at86_link(void);

#endif // TEST_AT86_LINK_H
