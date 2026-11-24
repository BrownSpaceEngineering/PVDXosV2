# TODO

Open items for CFDP, radio, and integration. UHF hardware/RF items are listed in `src/tasks/uhf_radio/uhf_radio_main.c`.

## ADCS
- `OPERATION_SET_KEPLER_COEFF` / `OPERATION_ADCS_UPDATE_PARAMS` are stubs: load from `MRAM_REGION_ADCS_UPLINK` and apply.
- `OPERATION_SET_UNIX_TIME` (UHF task, every ground packet) is a stub: decide whether it should set UTC.

## Data to downlink
- Camera task uses `capture_rtt()`; switch to `capture_mram()`.
- `PEROVSKITE_LOG_ONLY` is defined, so sweeps aren't saved to MRAM.
- Perovskite sweeps aren't CRC-checked before downlink (needs an incremental `crc32()`).

## Downlink
- Send pictures: only the last image is kept; a count > 1 sends one.
- Send IV curve: send *all* saved sweeps, not only the newest.
- `perovskite_data_ready_for_downlink` clears after any sweep is sent; clear only once all are.
- If the ground gets a whole file but its FIN is lost (pass ended, S-band failed, or an uplink overrode the send), the ready input stays set until the ground requests the file again. Transactions don't continue across passes, so a late FIN isn't expected. Decide whether the ground needs a way to clear it without a re-send.
- Duplicate send requests queue duplicate puts.
- Metadata has no file names; the ground can only tell files apart by sequence number.

## Uplink
- `TRANSMIT_ON_OFF` is a backup for the PUSP transmit flags; its byte is assumed 1 = on, 0 = off (TODO(temp)). Confirm with the sheet.
- Sheet gives no handling for Set ADCS opmode.
- Enable/disable device rejects states other than 0/1; confirm.
- ADCS parameters: device rotation encoding (3 bytes for two matrices?), flag bit order (assumed MSB-first), B-dot override / dipole duration ("1(?) hour").
- Enable/disable device: to be done through the device mock system. Ignored for now.
- Set power mode: to be done as a PUSP bitmap, through the state machine (FORCE_LOW, FORCE_HIGH, AUTO). The uplink command is ignored for now.
- Take IV curve sets the sweep's sampling resolution (steps per pixel, 2-64). Kept in RAM only, so it goes back to 64 on reboot; decide whether to save it to MRAM.
- Error handling in all tasks; the ground isn't told when a command fails.
- Real callsign: `UPLINK_CALLSIGN` and `spp_wrap()`.

## Passes and the state machine
- A lost pass input update (task manager queue full) is only logged: a lost "pass ended" leaves the satellite in a pass state until the next pass. Retry it.
- The reflash state must clear `uhf_pass_pending` / `sband_pass_pending` (or end the pass first) if it suspends the UHF task.
- CFDP doesn't run in LOW_POWER_IDLE, so there's no CFDP at low battery. Decide whether a UHF pass may start from LOW_POWER_IDLE.
- A pass that starts during a perovskite sweep or student picture waits until that finishes.
- CFDP's safe-suspend timeout is 3 s, but an STXG2 mode change can take up to 4 s; a misbehaving S-band radio while leaving a pass would `fatal()`.
- A restart SYN of the same pass type still gets a SYN_ACK and stays in the same pass (not ignored outright, since the ground would retry forever).
- The student capture states (`STATE_STUDENT_PICTURE_CAPTURE`, `STATE_STUDENT_CAPTURE_OVERRIDE`) have no timeout: if the display update or camera request fails, no photo is ever reported, so the satellite stays there and no pass state can start. Add one, like `PEROVSKITE_CAPTURE_TIMEOUT_MS`. The low-battery exit of `STATE_STUDENT_PICTURE_CAPTURE` is also commented out.

## S-band
- RF settings are placeholders (TODO(temp) in `radio_routing.c`): 2450 MHz, 5 Msps, MODCOD 1, 27 dBm. The flatsat needs 14-20 dBm, which the driver rejects.
- Framing is TODO(temp): CCSDS ASM `0x1ACFFC1D` before each USLP frame, `0x00` fill to 4 bytes. The ground decoder must match.
- Confirm in the STXG2 USM that SPI can feed the encoder in Transmit mode (`stxg2_stream_begin()`). The `sband/` docs are missing from the local PVDXdevref checkout.
- Tune `RADIO_SBAND_TURN_MS` (5 s timed retransmit turn on S-band passes) and `RADIO_SBAND_RETRY_MS` (30 s bring-up retry).

## Ground station
- PUSP: `PUSP_NOTIF_SBAND_PASS` (bit 2) on the SYN picks a UHF + S-band pass; on that pass the ground must keep the UHF turn and receive every CFDP PDU on S-band.
- Perovskite sweeps have a new `step_count` field (and `reserved`) before `current`; update the decoder. Sweeps saved before the change are rejected.
- On a UHF-only pass, images and perovskite sweeps come down on UHF (VCs 0 and 1).

## Time
- After reboot, UTC is restored from the last save, so it lags.
- Sweeps taken before UTC is set keep timestamp 0.

## Testing
- No end-to-end CFDP test against the ground station; MRAM send path untested on hardware.
- The new PUSP/pass tests haven't been run on the board (`make test` builds; results come over RTT).
- No tests for the pass state-machine inputs (`uhf_set_pass()` skips the update before the scheduler starts), the deferred uplink commands, or the student capture override.
