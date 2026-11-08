# PVDXos Repository Instructions

PVDXos is the real-time operating system for Brown Space Engineering's PVDX
satellite, running on an ATSAMD51P20A/SAMD51 Cortex-M4. The firmware is built
with the Arm GNU toolchain (`arm-none-eabi-*`) and runs on hardware such as the
Adafruit Grand Central development board.

## Architecture and hardware

- `bootloader/src/preboot.c` is the stage-1 image at flash `0x00000000`. It
  checksums eight independently linked `0x2000`-byte stage-2 copies in
  `0x00002000` through `0x00012000` and falls through to the next valid copy.
- `bootloader/src/bootloader.c` validates the image header and repairs damaged
  8 KiB OS blocks from the voted MRAM golden copy, then runs the OS in place.
  Flash has a reserved header row at `0x00080000`, the OS starts at
  The versionless 8 KiB header block is part of the OS image at `0x00080000`;
  executable code starts at `0x00082000`. The complete 512 KiB OS image,
  including its header, is mirrored to MRAM at `0x00000000`; the bootloader
  copy is at MRAM `0x00080000`. No header or checksums are stored in the unused
  flash gap. If MRAM is unavailable, the bootloader continues from flash.
- `src/` is the application; `src/src_flash.ld` links it for execution from
  flash at `0x00082000`. `src/main.c` initializes Atmel Start hardware,
  logging, startup checks, tasks, and FreeRTOS.
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
`scripts/create_flash_segment.py`, which assembles stage 1, eight stage-2 images,
the OS image header, and the flash-resident application into `flash.bin`.
`src/Makefile` passes
exactly one of `DEVBUILD`, `RELEASE`, or `UNITTEST`; do not combine these
manually. Tests are embedded firmware tests: `make test` calls `tests_run()` in
`src/main.c` before FreeRTOS, with results emitted through SEGGER RTT. Current
test functions cover SPP, linear algebra matrix multiplication, and CFDP
parsing. To build only some tests, pass their names, e.g.
`make test TESTS="spp cfdp"` (defines `TEST_SPP`/`TEST_CFDP`; see `TEST_NAMES`
in `src/Makefile` and `src/tests/test.h`); with no `TESTS`, all are built.
Builds fail if `PVDXos.bin` exceeds the 504 KiB app slot or the linker region
overflows.

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

- `src/Makefile` discovers `.c` files under `src/` with `find` and derives
  `OBJS` and `EXTRA_VPATH` from them (the generated `ASF/gcc/Makefile`
  consumes those lists); to keep a file or directory out of the build, add it
  to `EXCLUDE_PATHS`.
- `src/libraries/adcs` is the `adcs-c` submodule (with CMSIS-DSP nested inside
  it, so use `git submodule update --init --recursive`). It is excluded from
  the `find` glob: its sources, include dirs and flags come from its own
  `adcs.mk`. It shares the global symbol namespace with `src/linalg`, which
  defines generic names like `eye`, `dot`, `norm`, `inv` and `chol`.
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
  checksum locations across the generated stage-2 linker scripts,
  `src/src_flash.ld`, `bootloader/src/image_format.h`, and
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
