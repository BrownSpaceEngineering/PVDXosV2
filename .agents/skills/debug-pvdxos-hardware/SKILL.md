---
name: debug-pvdxos-hardware
description: Debug the PVDXos firmware (SAMD51P20A, FreeRTOS) on real hardware via J-Link, GDB and RTT logs. Use for hard faults, resets, hangs, task or stack problems, or inspecting registers, memory and backtraces on the running board. For building and flashing, use build-and-flash-pvdxos.
---

# Debugging PVDXos on hardware

Use only the wrappers in `tools/`. Never run these directly; they are interactive or never
terminate and will hang the session: `make connect`, `make connect_bl`, `gdb`,
`python3 scripts/rtt_logs.py`, `scripts/rtt_shell.py`, `scripts/rtt_splitscreen.py`.

## How this firmware is laid out (matters for debugging)

- Flash holds three bootloader copies (0x0, 0x3000, 0x6000) and three app copies. On boot the
  bootloader majority-votes the app and copies it to RAM at 0x20000000, then jumps to it.
  So the app runs from RAM, and GDB needs `src/PVDXos.elf` (linked for RAM) plus the bootloader ELFs.
  `tools/gdb.sh` loads all of them the same way `make connect` does.
- Flashing is done with `JLinkExe -CommanderScript flash.jlink` (the composite `flash.bin`),
  never GDB `load` (that would write the RAM-linked ELF to the wrong place). See build-and-flash-pvdxos.
- ELFs are gitignored: build first (`make dev`) and make sure the ELFs match what is flashed.

## Workflow

1. Build and flash if needed (build-and-flash-pvdxos skill; flashing needs user approval).
2. `tools/jlink.sh start` (skips if a server is already listening on :2331).
3. `tools/rtt.sh start`, then read the firmware log with `tools/rtt.sh tail 200`. Read logs first.
4. Snapshot the target (halts, runs commands, resumes):
   `tools/gdb.sh "bt" "rtos_current" "faults" "info threads"`
5. Catch a fault: `fatal()` resets the board about 1 s after a fault, so attaching afterwards shows
   a fresh boot. Instead: `GDB_TIMEOUT=90 tools/gdb.sh wait_fault`, then reproduce the problem.
   It stops in `PVDX_default_handler` and prints a backtrace and fault registers.
6. When done: `tools/rtt.sh stop` and `tools/jlink.sh stop`.

Macros in `tools/gdb/helpers.gdb`: `faults`, `rtos_current`, `tasks`, `reset_halt`, `wait_fault`
(`help <macro>` for details). The PVDXos shell is on telnet :19021 (RTT channel 0); the log is
channel 1, so they don't collide.

## Approval rules

- Safe without asking: reading registers, memory, backtraces, RTT log, `info threads`.
- Ask first: flashing, `make reset_mcu`, `make flash_monkey`, erasing, writing memory or registers, BOOTPROT fuses.

## Gotchas

- The hardware watchdog is enabled in every build (period 16384 cycles, about 16 s assuming the
  usual 1.024 kHz clock). Don't leave the core halted for long; `tools/gdb.sh` resumes by default.
  Use `GDB_RESUME=0` only for short inspections.
- If the PC is below 0x10000 the core is in a bootloader, and app symbols/backtraces are meaningless.
- After a reset, code in RAM is re-copied by the bootloader. Breakpoints set before a reset are lost.
- Task names are truncated to 8 characters. `info threads` may be empty if the J-Link FreeRTOS plugin
  can't find its symbols (the firmware doesn't define `uxTopUsedPriority`); try `JLINK_RTOS=0`
  if the server misbehaves, and use `rtos_current` instead.
- Don't repurpose PA30/PA31 (SWCLK/SWDIO); deep standby sleep drops the debug connection.
- Output is truncated at 200 lines (`GDB_MAXLINES`); prefer targeted commands over big dumps.
- Only one GDB client at a time on :2331. Don't leave your own interactive GDB attached.
