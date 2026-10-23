# Post-rework board smoke test

Build with `BOARD_SMOKE_TEST=1` to check the three MRAMs, RM3100 magnetometer,
SCH16T gyroscope, and TCA9546A mux. The test runs five cycles, logs to RTT
channel 1, and then idles without exercising the H-bridge or other actuators.
This is a bench diagnostic, not a production driver or full qualification test.

## Hardware and scope

- Mux uses the corrected normal SERCOM6 routing: **PD09 SDA, PD08 SCL**,
  address `0x70`, active-low reset **PB20** released high.
- No fuel gauges need to be attached. No fuel-gauge transactions are issued.
- Do not combine with `FG_SERCOM7_BENCH=1`; SERCOM7 is not used as an I²C workaround.
- SPI buses are configured for mode 0 at 1 MHz.
- MRAM checks read ID/status and compare two 16-byte reads at `0x000980`.
  **No write-enable, array writes, configuration writes, or protection changes**
  are issued. This does not test write functionality, full-array integrity, or retention.
- RM3100 checks identity, volatile configuration readback, data-ready and XYZ data
  using the PCB's sensor SPI bus (not the display bus).
- SCH16T discovers its target address, performs a software reset and volatile
  sensing initialization, then checks CRC, response address/type, diagnostic status,
  angular rate, acceleration, and temperature. Samples are polled; DRDY/freshness
  is not independently checked.
- Mux checks control-register write/readback for `00/01/02/04/08`, then restores
  and verifies `00` (all channels off). This does not prove downstream connectivity.
- Diagnostic RTT buffer size is 16384 bytes; normal builds retain 2048 bytes.

## Build, flash, capture

From the repository root, with the existing Python environment containing
`pylink-square` activated:

```sh
make clean BOARD_SMOKE_TEST=1
make -C bootloader -j8
make -C src -j8 dev BOARD_SMOKE_TEST=1
python3 scripts/create_flash_segment.py
JLinkExe -CommanderScript flash.jlink
python3 -u tools/rtt_probe.py --seconds 12 --output logs/board-smoke-rerun.log
```

Run commands sequentially and stop if any fails. Calling the subdirectory makes
directly allows `-j8` to work around the root Makefile's recursive jobserver warning.
Use a fresh log filename for each run. Flash the composite `flash.bin` using
`flash.jlink`, not a debugger `load` of the RAM-linked application ELF.

Clean before switching build flags: existing object files do not automatically
rebuild just because `BOARD_SMOKE_TEST` changes. **The default application on this
branch exercises the Y-axis H-bridge**; do not flash it as a harmless idle build.

## Second reworked board — smoke verification

Capture: `logs/board2-post-rework-smoke-initial.log`.

```text
SUMMARY /5 MRAM1=5 MRAM2=5 MRAM3=5 RM3100=5 GYRO=5 MUX=5
```

Clean-built with `make -C src -j8 dev BOARD_SMOKE_TEST=1`, flashed, and captured
five complete cycles. All three MRAMs returned `6B BB 14`, status `00` (WIP/WEL
clear), and matching repeated reads at `0x000980`. RM3100 configuration/data-ready
and live XYZ passed; SCH16T initialization, diagnostic status and live readings
passed (temperature about 21.9 °C at the last sample). The mux passed all control
register patterns on normal SERCOM6 wiring and was restored to all channels off.

No MRAM writes or factory initialization were performed on this second board;
this verifies identity and sampled read consistency, not write/retention behavior.
Firmware reached the idle loop without actuator exercise. The first board's
initialization and stronger MRAM write-test results below do not apply to this board.

## First board — verification after authorized MRAM initialization

Capture: `logs/post-rework-board-check-after-mram-init.log`.

```text
SUMMARY /5 MRAM1=5 MRAM2=5 MRAM3=5 RM3100=5 GYRO=5 MUX=5
```

All six devices passed. This final image is a clean `dev BOARD_SMOKE_TEST=1`
build: neither `hardware_mram_check` nor `hardware_mram_factory_initialize` is
present in the ELF. MRAM testing in this image is read-only. Firmware completed
and idles without actuators; all mux channels are off.

With the user's explicit authorization to overwrite MRAM, the follow-up found:

1. Before initialization, repeated reads were unreliable at 50 kHz, 1 MHz, and
   2 MHz, with both split and combined HAL transfers. JESD252 reset alone did not
   fix this. After clearing protection, sampled write/read tests passed chip 1,
   but single-bit readback errors occurred on chips 2 and 3.
   Capture: `logs/mram-post-rework-comparison.log`.
2. Ported the audited EST3000 factory procedure into the opt-in bare-metal helper
   `src/tests/hardware_mram_factory.c`. All three chips completed status/NV/OTP
   initialization and four full 1 MiB array write/verify passes (`00/FF/00/FF`)
   per chip, followed by verified DFIM exit. Captures:
   `logs/mram-post-rework-factory-cont.log` and
   `logs/mram-post-rework-factory-final.log`.
3. Removed the factory-enabled image and rebuilt/flashed the nonfactory MRAM
   diagnostic. All three chips passed five known patterns at each of three
   512-byte windows, each read back three times, at 2 MHz. All 54 stability rows
   (three chips, three clocks, two HAL transfer styles, before/after reset and
   after patterns) were `30/30`. Capture:
   `logs/mram-post-rework-nonfactory-verification.log`.
4. Rebuilt/flashed the smoke-only image and obtained the all-pass summary above.

The first factory capture (`logs/mram-post-rework-factory.log`) ended with a
J-Link/Python error and an additional startup was observed in the next capture;
that partial attempt is not the basis for claiming complete initialization.
The later continuation/final captures record a complete successful procedure.

Factory initialization eliminated the observed instability, but does not isolate
whether the cause was post-rework memory state, configuration, or another effect
of that multi-step procedure. These checks are not retention, cold-power-cycle,
full-array address-aliasing, or environmental qualification tests. MRAM contents,
OTP, and configuration were overwritten; sampled regions contain the subsequent
test patterns, not uniformly `FF`. The final smoke log shows WIP clear and WEL set
from preceding write tests; no further writes are issued by the idle firmware.

## Initial post-rework result (before MRAM initialization)

Capture: `logs/post-rework-board-check-final.log`.

```text
SUMMARY /5 MRAM1=0 MRAM2=2 MRAM3=0 RM3100=5 GYRO=5 MUX=5
```

- Mux, magnetometer, and gyro passed all five cycles. Gyro target address was `3`.
- All three MRAMs repeatedly returned ID `6B BB 14`; status was respectively
  `8C`, `DC`, and `88`, with WIP/WEL clear.
- MRAM repeated array reads matched in 0/5, 2/5, and 0/5 cycles respectively.
  Communication is confirmed, but reliable array reads are **not established**.
  This alone does not establish PCB damage or distinguish a configuration/timing,
  signal-integrity, supply, or device issue.
- No MRAM contents were changed. Firmware reached its idle loop; mux channels
  were left disabled. Sensors remain configured for sensing until reset/power-off.

## Relevant MRAM branch history

Inspected local branches without checking them out or changing the flashed image:

- `yi-mram-test`, commit `b29f45a3`: `src/checks/device_checks.c` uses JESD252
  interface-reset recovery, then checked ID/status/flag reads and write/restore
  tests. It fixes the block-protection mask to `0x5C` (BP bits are not contiguous),
  verifies WEL, and checks completion/error flags. Local capture
  `logs/mram-unprotect-final-verification.log` records all three chips passing.
- `yi-mram-driver-audit`, commit `5eb36132`: `scripts/README.md` records an earlier
  MRAM2 single-bit restoration failure that did not recur after verified EST3000
  factory initialization. It explicitly does not establish the original cause.
  `logs/mram-audit-default-50khz-verification.log` confirms the later non-factory
  test passed `43/43`, including original-data restoration on all three chips.
- The prior successful audit run used 50 kHz SPI, whereas this smoke test uses
  1 MHz. Prior drivers also send the command/address and read payload in separate
  HAL calls while holding CS low; this smoke test uses one full-duplex transfer.
  These differences warrant controlled comparisons, not assumptions of equivalence.
- Current status bytes contain block-protection bits, unlike the prior successful
  audit run. Protection clearing matters for writes; it is not an explanation
  for inconsistent read-only array data and must not be applied as a speculative fix.

Do **not** blindly flash either branch's complete image: its boot path/tests may
write MRAM. Factory initialization overwrites array, OTP and configuration contents
and requires explicit authorization; it is not part of the normal smoke test.

## Opt-in destructive MRAM diagnostics

The standalone diagnostic lives in `src/tests/hardware_mram_check.c`. It intercepts
the smoke harness and idles afterward; the other sensors are not tested in this
mode. Explicit board diagnostics take precedence over actuator-capable unit tests,
but use the `dev` target shown below.

**Both modes are destructive.** Even without factory initialization, the diagnostic
clears block protection when needed and overwrites three sampled windows per chip
without restoration. The factory option additionally overwrites the full arrays,
OTP, and configuration, and repeats on every boot until that image is removed.

```sh
# Nonfactory diagnostic; clean with flags matching the previous image first.
make -C src clean BOARD_SMOKE_TEST=1 BOARD_MRAM_TEST=1 BOARD_MRAM_FACTORY_INIT=1
make -C src -j8 dev BOARD_SMOKE_TEST=1 BOARD_MRAM_TEST=1
python3 scripts/create_flash_segment.py
# Flash using flash.jlink; capture with a fresh filename as above.
```

To run the full factory procedure, add `BOARD_MRAM_FACTORY_INIT=1` to the clean
and build commands. Allow several minutes and capture continuously; reconnect
for bounded captures with new filenames if needed, without deliberately resetting
an in-progress procedure. Require all three completion records and `FACTORY=PASS`,
not merely a successful flash or partial array progress. Remove the factory image
with a clean rebuild and flash of the smoke-only firmware, then rerun the smoke test.

Stability sweeps compare repeated reads at three 16-byte windows; they do not prove
known-data correctness at every clock. Known-pattern checks use three 512-byte
windows, five patterns, triple readback, and split transfers at 2 MHz. Inspect sweep
counts as well as the write/read summary: `COMPLETE` alone is not an overall pass.
