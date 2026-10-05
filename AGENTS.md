# PVDXos Repository Instructions

PVDXos is the real-time operating system for Brown Space Engineering's PVDX
satellite, running on an ATSAMD51P20A/SAMD51 Cortex-M4. The firmware is built
with the Arm GNU toolchain (`arm-none-eabi-*`) and runs on hardware such as the
Adafruit Grand Central development board.

## Architecture and hardware

- `bootloader/` contains three independently linked `0x3000`-byte bootloader
  images at flash offsets `0x00000000`, `0x00003000`, and `0x00006000`.
  `startup.c` validates checksums and falls through to the next copy if needed.
  `bootloader.c` byte-wise majority-votes application copies at
  `0x00010000`, `0x00020000`, and `0x00030000`, copies the result to RAM at
  `0x20000000`, sets VTOR, and jumps to it.
- `src/` is the application; `src/src_ram.ld` links it for RAM execution after
  the bootloader copy. `src/main.c` initializes Atmel Start hardware, logging,
  startup checks, tasks, and FreeRTOS.
- `ASF/` is generated Atmel Start/HAL/FreeRTOS support. Drivers are under
  `src/drivers/`, CCSDS protocols under `src/ccsds/`, and numerical routines
  under `src/linalg/`. Host-only utilities are under `scripts/`.
- Application tasks use static FreeRTOS allocation. `src/tasks/task_list.c`
  owns task records, priorities, queues, stacks, watchdog metadata, and enabled
  state. The first three `task_list` entries must remain, in order: watchdog,
  command dispatcher, task manager.
- A disabled task is still created and then suspended; enabling or disabling it
  registers or unregisters it with the watchdog.
- Hardware flashing and debugging assume a SEGGER J-Link, target
  `ATSAMD51P20A`, SWD, and (for GDB) server port 2331. The shell uses
  `localhost:19021` when enabled, and RTT diagnostics use the existing SEGGER
  RTT path.

## Build, test, and debug

Run commands from the repository root:

```bash
make
make dev
make clean all
make release
make test
make -C bootloader
make -C src dev
make clean
```

`make dev`, `make release`, and `make test` rebuild the bootloader and run
`scripts/create_flash_segment.py`, which assembles the three bootloader images,
checksums, and three application copies into `flash.bin`. `src/Makefile` passes
exactly one of `DEVBUILD`, `RELEASE`, or `UNITTEST`; do not combine these
manually. Tests are embedded firmware tests: `make test` calls `tests_run()` in
`src/main.c` before FreeRTOS, with results emitted through SEGGER RTT. Current
test functions cover SPP, linear algebra matrix multiplication, and CFDP
parsing. To run one test function, temporarily leave only that call in
`src/tests/test.c:tests_run()`, build, flash/connect, and inspect RTT output.

For hardware flashing, start a J-Link GDB server configured for
`ATSAMD51P20A`, SWD, and port 2331. For J-Link/GDB workflows:

```bash
make clean all connect
make connect
make connect_bl
JLinkExe -CommanderScript flash.jlink
make reset_mcu
python3 scripts/rtt_logs.py
```

`make flash_monkey` intentionally flips random bits in `flash.bin`; rebuild
before using a normal image again. Build documentation with
`doxygen Doxyfile`. Format C/C++ with the repository `.clang-format`
configuration, for example `clang-format -i path/to/file.c`.

## Persistent conventions and constraints

- When adding a `.c` file, update `OBJS` and, for a new directory,
  `EXTRA_VPATH` in `src/Makefile`; the generated `ASF/gcc/Makefile` consumes
  those lists. Header-only additions do not need an object entry.
- Treat `ASF/` as generated output. Change Atmel Start configuration through
  the `PVDX-SAMD-PinConfig` submodule and run `make -C src update_asf` only
  when intentionally replacing ASF; that target deletes and recreates `ASF/`
  and requires an `.atzip` file in the submodule.
- Firmware builds require GNU99 and exactly one build-type macro. Compiler
  warnings are errors, and branch plus abbreviated commit metadata is compiled
  into the application.
- Keep hardware diagnostics on the existing logging/SEGGER RTT path and use
  the project's `fatal`, `warning`, `info`, `debug`, and assertion helpers.
- Follow `.clang-format`: Google-derived style, four-space indentation, no
  tabs, right-aligned pointer declarators (`int *p`), and a 140-column limit.
- Linker scripts and flash offsets are part of the redundancy protocol.
  Coordinate any change to bootloader sizes, application addresses, or
  checksum locations across the three bootloader linker scripts,
  `src/src_ram.ld`, `bootloader/bootloader.c`, and
  `scripts/create_flash_segment.py`.

Shared procedures with detailed build/flash or logic-analyzer workflows are
documented in `.agents/skills/`.

## Sibling repository: PVDXdevref

The team's datasheets and development reference materials live in the
**PVDXdevref** repository, which should be placed as a sibling of this
repository so that the layout on disk is:

```
PVDX/
├── PVDXdevref/  ← datasheets and development reference materials
└── PVDXosV2/   ← this repository
```

**Agent startup check:** at the beginning of every session, verify that
`../PVDXdevref` exists relative to this repository root (i.e. check whether the
path `../PVDXdevref` is a directory). If it is absent, immediately tell the user:

> ⚠️ The **PVDXdevref** sibling repository was not found at `../PVDXdevref`.
> Please clone it alongside this repo at `../PVDXdevref`.
> Hardware datasheets and development reference materials used by tasks in this
> repository live there.

<!-- BEGIN debug-tooling (added by setup-debug-tooling.sh) -->

## Agent-safe hardware debugging

Several commands above are interactive or never terminate. Agents must NOT run `make connect`,
`make connect_bl`, `gdb` directly, `python3 scripts/rtt_logs.py`, `scripts/rtt_shell.py` or
`scripts/rtt_splitscreen.py`. Use these wrappers instead (details in the `debug-pvdxos-hardware` skill):

- GDB server: `tools/jlink.sh start|stop|status`
- Firmware log (RTT channel 1) in the background: `tools/rtt.sh start`, `tools/rtt.sh tail 200`, `tools/rtt.sh stop`
- Inspect the running target (safe, read-only): `tools/gdb.sh "bt" "rtos_current" "faults" "info threads"`
- Catch a fault (board resets ~1 s after `fatal()`): `GDB_TIMEOUT=90 tools/gdb.sh wait_fault`
- Build/flash: see the `build-and-flash-pvdxos` skill. Flashing uses `flash.jlink`, never GDB `load`
  (the app is linked for RAM and copied there by the bootloader).

Rules:

- Reading registers, memory, backtraces and RTT logs is fine. Flashing, resetting, erasing, or writing
  memory/registers needs user approval.
- Read the RTT log first; use GDB snapshots to confirm.
- The hardware watchdog is always on (about 16 s); don't leave the core halted.

<!-- END debug-tooling -->
