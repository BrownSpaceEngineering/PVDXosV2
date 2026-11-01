#ifndef TEST_STXG2_H
#define TEST_STXG2_H

// Station ID for the S-band transmit test. The radio sends "DE <callsign> PVDX S-BAND TEST" in its own transfer frame at
// the end of each transmission, and every 9 minutes in longer ones. Replace the placeholder with the control operator's
// amateur callsign before any over-the-air test. 1 to 10 characters.
#define STXG2_TEST_CALLSIGN "N0CALL"

void test_stxg2(void);
void test_stxg2_sweep(void);

#endif // TEST_STXG2_H
