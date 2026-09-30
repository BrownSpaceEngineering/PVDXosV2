---
name: capture-samd51-logic-analyzer
description: "Capture and analyze SAMD51 board signals with sigrok-cli; use when a task requires the repository's fx2lafw logic-analyzer setup, reset timing, or capture validation workflow."
---

# Capture and Analyze the Logic Analyzer

Use this skill when you need to capture board activity with the USB logic analyzer and inspect the recorded data.

## Device And Channel Map

- Analyzer driver: `fx2lafw`
- Channels used for the board reset check:
  - `D0` -> `PA01`
  - `D1` -> `PA00`
  - `D2` -> `PD11`
  - `D3` -> `PD12`
  - `D4` -> `PC11`

## Important Caveat

`sigrok-cli` cannot open the analyzer while PulseView is already attached to the USB device. If capture fails with an error like `Unable to claim USB interface`, close PulseView first or stop the `pulseview` process, then retry.

## Known Good Commands

First confirm the device is visible:

```bash
sigrok-cli --scan
```

Inspect the connected analyzer details and supported sample rates:

```bash
sigrok-cli -d fx2lafw --show
```

Capture a short reset window while triggering the MCU reset flow:

```bash
cd /home/yi/dev/PVDXosV2
sigrok-cli -d fx2lafw -c samplerate=24000000 -C D0,D1,D2,D3,D4 --time 500 -o reset_capture.sr &
CAP_PID=$!
sleep 0.05
make reset_mcu
wait "$CAP_PID"
sigrok-cli -i reset_capture.sr -o reset_capture.csv -O csv
```

To use a trigger, sigrok expects the channel name followed by `=` and the trigger code. For example, a falling-edge trigger on D0 is:

```bash
sigrok-cli -d fx2lafw -c samplerate=12000000 -C D0,D1,D2,D3,D4 -t D0=f --wait-trigger --samples 20000000 -o reset_triggered_12mhz.sr
```

That syntax is the one that worked in this workspace after closing PulseView so the USB interface was free.

## How To Check The Capture

Look for either of these signs that the capture contains useful activity:

- A first-change sample exists in the CSV when the samples are scanned in order.
- The unique row patterns are more than one constant line.

If the CSV contains only one repeated row pattern, the capture is technically valid but not informative for the reset event.

If the capture shows multiple patterns and a nonzero first-change sample, the trigger likely locked onto real activity.

## Observed Result In This Repo

The reset capture completed successfully after PulseView was closed, but the recorded channels stayed constant across the whole window. That means the capture showed stable logic levels, not an edge on the selected pins.

After switching to the correct trigger form, the capture did show multiple patterns and a first change near the beginning of the file, which means the trigger and capture path are working.

When that happens, use one of these follow-up checks:

- Trigger on a pin that is guaranteed to move.
- Capture a different reset-related signal, such as the board reset line, if available.
- Increase the window if the event is too brief.

## Analysis Tip

For a quick sanity check on the exported CSV:

```bash
awk 'NR>5{ if(prev=="") {prev=$0; next} if($0!=prev){print NR-5; exit} }' reset_capture.csv
tail -n +6 reset_capture.csv | sort | uniq -c | head
```

If the first command prints nothing and the unique-count output shows one row only, the capture was flat.
