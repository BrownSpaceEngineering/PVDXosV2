# Scripts
The scripts folder is comprised of a collection of scripts that are used to automate various tasks or interact with PVDXos in some way. They are *never* linked together with the main application (i.e. are not included in PVDXos itself).
## AT86RF215 link test

These scripts go with the firmware test in `src/tests/test_at86_link.c`. Build that firmware with `make test TESTS="at86_link"`.

- `hackrf_link.py` is the HackRF side of the MR-FSK exchange. It needs GNU Radio 3.10 and gr-osmosdr. Run `--selftest` first. That mode needs no hardware: it sends generated bursts through the same filter and demodulator.
- `at86_selftest.py` checks a running board through the RTT log. It confirms that the SAMD51 talks to the AT86RF215 over SPI, that the front end configuration is correct, and that the test loop keeps running. The firmware prints a heartbeat line every 2 s, and the script checks it. Add `--gdb` to read the `at86_link_status` counters over GDB, or `--file` to check a saved log.

Bring-up order:

1. Run `python3 scripts/hackrf_link.py --selftest`.
2. Flash the board. Then run `tools/rtt.sh start` and `python3 scripts/at86_selftest.py`.
3. Run `python3 scripts/hackrf_link.py --listen`. Every AT86 DATA frame must show `crc` ok. If the frames show `CRC_BAD` or `INVERTED`, the bit order or the polarity is different from the AT86.
4. Run `python3 scripts/hackrf_link.py` for the full exchange. While it runs, run `python3 scripts/at86_selftest.py --expect-traffic --duration 300`.

Set the same call sign in `LINK_CALLSIGN` and in `--callsign` before you transmit over the air.
