# AT86RF215 to HackRF link test

Run all commands from the repository root.

## Setup

1. Install GNU Radio, gr-osmosdr and the HackRF tools with the system package manager. On Fedora: `sudo dnf install gnuradio gr-osmosdr hackrf`
2. Install the Python packages: `python3 -m pip install --user -r scripts/requirements.txt`

## Wiring (ATREB215-XPRO EXT header to Grand Central)

| EXT pin | Signal | Grand Central |
| --- | --- | --- |
| 15 | CS | #25 (PC16) |
| 16 | MOSI | #41 (PC12) |
| 17 | MISO | #53, labeled SS (PD10) |
| 18 | SCK | #40 (PC13) |
| 7 | RESET | #24 (PC17) |
| 20 | VCC_TARGET | 3.3V |
| 19 | GND | GND |

Install the J1 jumper on the radio board. Without it, the radio has no power.

## AT86 side (SAMD51 + AT86RF215)

1. Build the firmware: `make test TESTS="at86_link"`
2. Flash the board: `JLinkExe -CommanderScript flash.jlink`
3. Start the log capture: `tools/rtt.sh start`
4. Run the self-test: `python3 scripts/at86_selftest.py --duration 60`

Expected result: the script prints one row for each check and ends with `at86 selftest: PASS`.
The AT86 sends DATA about every 6 s. After 10 tries, it starts the next round with the next sequence number.
The `test phase` row shows `A_TX_DATA` or `A_WAIT_ACK`, and the `link traffic` row shows no ACKs.

If the self-test shows `raw_pn=0x00`, the radio does not answer on SPI. Check the wiring, the power and the J1 jumper.

## HackRF side

1. Check the script without hardware: `python3 scripts/hackrf_link.py --selftest`

   Expected result: nine `PASS` lines, then `selftest: PASS`.

2. Check the HackRF: `python3 scripts/hackrf_link.py --check-device`

   Expected result: `PASS  hackrf_info sees a device` and `PASS  1 s RX capture through GNU Radio`.

3. Receive only, to see the AT86 frames: `python3 scripts/hackrf_link.py --listen`

   Expected result: one `hrfpkt rx DATA` line about every 6 s, with `call=<CALLSIGN>` and the payload `PVDX AT86 round N`. The sequence number stays the same for 10 lines, then goes up by 1. No line shows `CRC_BAD` or `INVERTED`. The frequency offset (`foff`) stays within a few kHz of zero. If you see `CRC_BAD`, the signal is too weak or too strong. Change the attenuation or the gain.

4. Run the full exchange: `python3 scripts/hackrf_link.py`

   Expected result: one `hrfround` line with `a=ok b=ok` about every 35 s. The next section shows a full round.

## Both sides together

While step 4 of the HackRF side runs, run this on the AT86 side:

```
python3 scripts/at86_selftest.py --expect-traffic --duration 300
```

Expected result: `at86 selftest: PASS`. In the `link traffic with the HackRF` row, `ackrx`, `datarx` and `rounds ok` go up, and `fail` does not change. A round takes about 35 s, so 300 s gives about 8 rounds.

## A good round

The HackRF log shows these lines in this order:

1. `hrfpkt rx DATA seq=N`: the HackRF received the AT86 DATA.
2. `hrfpkt tx ACK seq=N`: the HackRF sent its ACK.
3. About 15 s with no lines, while the HackRF listens for repeats of the same DATA.
4. `hrfpkt tx DATA seq=N`: the HackRF sent its own DATA.
5. `hrfpkt rx ACK seq=N`: the AT86 ACK arrived.
6. `hrfround n=N a=ok b=ok`.

The AT86 log shows the same exchange in `at86pkt` lines. Each round ends with `at86round n=N a=ok b=ok`.

Some packet loss is normal. Both sides recover from these cases without help:

- `hrfpkt no ACK for seq=N try=1`, then later `hrfpkt rx ACK seq=N`: the HackRF sent its DATA again. The round passes.
- Two `hrfpkt tx ACK` lines with the same sequence number: the AT86 did not hear the first ACK and sent its DATA again. The `dup` counter goes up. The round passes.
- `at86pkt rx DATA ... in phase A`: all HackRF ACKs were lost, but HackRF DATA shows that the AT86 DATA arrived. The `ackimp` counter goes up. The round passes.
- `hrfpkt new AT86 DATA ... in phase B`: the AT86 stopped waiting for HackRF DATA. This round fails on both sides, and the next round starts with that DATA.

If one side hears nothing from the other side for all of its tries, the round fails.

## Turnaround timing

The HackRF cannot transmit and receive at the same time. It closes and opens the device over USB at each switch, which takes about a second. The HackRF log shows two times:
- `turnaround=` on `tx ACK` lines: the time from the received DATA to the end of the HackRF ACK. It must stay below `LINK_ACK_TIMEOUT_MS` (6 s). After that time, the AT86 stops listening for the ACK.
- `rxready=` on `rx` and `no ACK` lines: the time that the HackRF RX takes to start. It must stay well below `LINK_ACK_DELAY_MS` (2 s). The AT86 sends its ACK after that delay.

If a time comes close to its limit, increase the limit. Then keep these rules:

- `LINK_DUP_WINDOW_MS` (15 s) is longer than one HackRF retry: the `tx=` time plus `--ack-timeout`.
- `--dup-window` (15 s) is longer than one AT86 retry: `LINK_ACK_TIMEOUT_MS` plus 0.1 s.
- `--ack-timeout` (6 s) is longer than `LINK_ACK_DELAY_MS` plus `rxready`.
- `LINK_DATA_WAIT_MS` (120 s) is longer than `--dup-window` plus all HackRF DATA tries (`--tries` times one HackRF retry).
- `--data-wait` (150 s) is longer than `LINK_DATA_WAIT_MS`.

If you change `LINK_DATA_WAIT_MS`, also change `MAX_IDLE_RADIO_S` in `scripts/at86_selftest.py` to a slightly larger value.

## Settings to keep the same on both sides

- Frequency: `LINK_FREQ_HZ` in `src/tests/test_at86_link.c` and `--freq`. The default is 437.765 MHz.
- Call sign: `LINK_CALLSIGN` in `src/tests/at86_link_packet.h` and `--callsign`. Set your licensed call sign before you transmit.
- AT86 TX power: `LINK_TX_POWER` in `src/tests/test_at86_link.c` (0 to 31).
- HackRF TX power: `--tx-vga` (0 to 47 dB) and `--tx-amp` (0 or 14 dB).

Put attenuation between the AT86 and the HackRF. The HackRF input must stay below -5 dBm.
