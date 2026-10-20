# AT86RF215 to HackRF link test

Run all commands from the repository root.

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

If the self-test shows `raw_pn=0x00`, the radio does not answer on SPI. Check the wiring, the power and the J1 jumper.

## HackRF side

1. Check the script without hardware: `python3 scripts/hackrf_link.py --selftest`
2. Check the HackRF: `python3 scripts/hackrf_link.py --check-device`
3. Receive only, to see the AT86 frames: `python3 scripts/hackrf_link.py --listen`
4. Run the full exchange: `python3 scripts/hackrf_link.py`

## Both sides together

While step 4 of the HackRF side runs, run this on the AT86 side:

```
python3 scripts/at86_selftest.py --expect-traffic --duration 300
```

## Settings to keep the same on both sides

- Frequency: `LINK_FREQ_HZ` in `src/tests/test_at86_link.c` and `--freq`. The default is 437.765 MHz.
- Call sign: `LINK_CALLSIGN` in `src/tests/at86_link_packet.h` and `--callsign`. Set your licensed call sign before you transmit.
- AT86 TX power: `LINK_TX_POWER` in `src/tests/test_at86_link.c` (0 to 31).
- HackRF TX power: `--tx-vga` (0 to 47 dB) and `--tx-amp` (0 or 14 dB).

Put attenuation between the AT86 and the HackRF. The HackRF input must stay below -5 dBm.
