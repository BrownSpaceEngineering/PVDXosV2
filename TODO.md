# TODO

Open items for CFDP, radio, and integration. UHF hardware/RF items are listed in `src/tasks/uhf_radio/uhf_radio_main.c`.

## Blocking
- ADCS uplink commands crash: `OPERATION_SET_UNIX_TIME`, `SET_KEPLER_COEFF`, `SET_ADCS_OP_MODE`, `ADCS_UPDATE_PARAMS` hit `fatal()` in `exec_command_adcs()`. Still true on `adcs-body-integration`.
- Merge `origin/adcs-body-integration` (newest ADCS; `main` has nothing new). Commit first; expect conflicts in `device_checks.c`, `cfdp_utils.c`.

## ADCS
- `OPERATION_SET_KEPLER_COEFF` / `OPERATION_ADCS_UPDATE_PARAMS` are stubs: load from `MRAM_REGION_ADCS_UPLINK` and apply.
- `OPERATION_SET_UNIX_TIME` (UHF task, every ground packet) is a stub: decide whether it should set UTC.

## Data to downlink
- Camera task uses `capture_rtt()`; switch to `capture_mram()`.
- `PEROVSKITE_LOG_ONLY` is defined, so sweeps aren't saved to MRAM.
- Perovskite sweeps aren't CRC-checked before downlink (needs an incremental `crc32()`).
- Nothing stops a capture/sweep overwriting a file mid-transfer.

## Downlink
- Send pictures: only the last image is kept; a count > 1 sends one.
- Send IV curve: only the newest sweep is sent; track unsent sequence numbers.
- `perovskite_data_ready_for_downlink` clears after any sweep is sent; clear only once all are.
- A FIN arriving after its txn was dropped (pass ended) doesn't clear the ready input.
- Duplicate send requests queue duplicate puts.
- IDLE → SBAND_TRANS is commented out in `states.c`; decide whether to re-enable.
- Metadata has no file names; the ground can only tell files apart by sequence number.

## Uplink
- Command numbers: the sheet now numbers them 1-13; the enum starts at 0. Pick one.
- `TRANSMIT_ON_OFF`: meaning of its byte; command or PUSP notifications only?
- Sheet gives no handling for Reboot or Set ADCS opmode; power mode values undefined.
- Enable/disable device rejects states other than 0/1; confirm.
- ADCS parameters: device rotation encoding (3 bytes for two matrices?), flag bit order (assumed MSB-first), B-dot override / dipole duration ("1(?) hour").
- Not routed to a task: enable/disable device (add to state machine inputs), take picture, take IV curve.
- Take IV curve: sweep can't use the sampling resolution (fixed 64 steps).
- Task manager drops Reboot, Transmit on/off, Set power mode.
- Error handling in all tasks; the ground isn't told when a command fails.
- Real callsign: `UPLINK_CALLSIGN` and `spp_wrap()`.

## Time
- After reboot, UTC is restored from the last save, so it lags.
- Sweeps taken before UTC is set keep timestamp 0.

## Testing
- No end-to-end CFDP test against the ground station; MRAM send path untested on hardware.
