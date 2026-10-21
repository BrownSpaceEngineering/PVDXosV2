# Fuel-gauge mux bench test

The current PCB swaps the upstream SDA/SCL traces relative to the normal
SERCOM6 mapping. For the bench only, SERCOM7 function C provides:

- PD08: SDA (Grand Central D51)
- PD09: SCL (Grand Central D52)
- PB20 / EPS_RST: active-low mux reset, driven high before testing
- TCA9546A address: 0x70 with A2:A0 low

## Build and capture

From the repository root:

```sh
make clean all FG_SERCOM7_BENCH=1
JLinkExe -CommanderScript flash.jlink
python3 tools/rtt_probe.py --seconds 22 --output logs/mux-only-sercom7.log
```

Use a Python environment with `pylink-square`. Flashing resets the target.
Always clean when switching build modes; the makefiles do not track flag changes.

`FG_SERCOM7_BENCH=1` also enables the mux-only test. It tests control bytes
0x00, 0x01, 0x02, 0x04 and 0x08, then writes and verifies 0x00 to leave all
channels disconnected between cycles. Results go to RTT channel 1 every five
seconds. No fuel-gauge reads or H-bridge exercise are performed.

The bench path deinitializes the startup-configured UHF SPI peripheral,
disconnects its pads, disables SERCOM6 I2C, and reinitializes the I2C_CAMERA
descriptor on SERCOM7 with the swapped pin mapping. Existing generated clock
initialization supplies SERCOM7's clock. The forced-included bench config adds
SERCOM7 I2C timing to the ASF configuration table without editing generated files.

**UHF SPI is unavailable in this mode. This is not a production fix.** The
bench config rejects release and unit-test builds. Without this explicit flag,
the normal SERCOM6/UHF configuration is unchanged; this branch's default main
runs the H-bridge test, not this mux test.

The existing analyzer probes now decode as CH0=SCL (PD09), CH1=SDA (PD08),
consistent with the PCB swap. Keep that mapping when viewing the saved capture:

```sh
sigrok-cli -d fx2lafw -c samplerate=2m -C D0,D1 --time 6000 -o logs/mux-sercom7-i2c.sr
sigrok-cli -i logs/mux-sercom7-i2c.sr -P i2c:sda=D1:scl=D0
```

If you move the probes to CH0=SDA and CH1=SCL, reverse the decoder assignments.

Successful control readback verifies the upstream interface and channel-control
register, not downstream electrical continuity. A read transaction ends with a
normal MCU-generated NACK after its single data byte; that is not a mux failure.
