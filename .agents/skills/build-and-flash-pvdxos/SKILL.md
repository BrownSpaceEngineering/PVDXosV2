---
name: build-and-flash-pvdxos
description: "Build, flash, and verify the PVDXos SAMD51 firmware with SEGGER J-Link; use when a task requires the repository's hardware build or standalone flashing workflow."
---

# Build and Flash PVDXos

Use this skill when a future agent needs the standard build and flash flow for this repository.

## Build

Run the build from the repository root:

```bash
make clean all
```

This builds the bootloader, builds the main firmware, and runs `scripts/create_flash_segment.py` to assemble `flash.bin`.

## Flash

With a SEGGER J-Link connected to the board, flash from the repository root:

```bash
JLinkExe -CommanderScript flash.jlink
```

The script targets `ATSAMD51P20A`, uses SWD at 4000 kHz, loads `flash.bin` at address `0x00000000`, resets the MCU, and starts execution.

### Reset-only (test)

A reset-only flow is provided for quick test resets without flashing. Agents should use the Make target or the J-Link script directly:

- Make target: `make reset_mcu` (invokes the `reset.jlink` CommanderScript)
- Direct J-Link: `JLinkExe -CommanderScript reset.jlink`

`reset.jlink` issues a `reset` over SWD at 4000 kHz and exits; use this when you need to trigger a hardware reset from an automated test or CI step.

## How To Confirm Success

Treat the flash as successful when the J-Link session shows all of the following:

- The target connects as `ATSAMD51P20A` over SWD.
- `loadbin flash.bin, 0x00000000` completes without error.
- J-Link reports `O.K.` after the download.
- The log shows `Program & Verify` completed with no failure message.
- The script reaches `Script processing completed.`

## Common Follow-Up Checks

- If the board does not appear to run after flashing, check RTT logs or the LED pattern expected by the active build configuration.
- If a factory-new SAMD51 board refuses to flash, clear the `BOOTPROT` fuse first, then retry the build and flash flow.
- If `flash.bin` is missing, rebuild with `make clean all` before retrying J-Link.

## Notes For Agents

- Prefer the repo-root build and flash commands above unless the user asks for a debug session.
- For debugging instead of raw flashing, the repository also supports the GDB flow described in the README, but this skill is focused on the standalone flash path.
