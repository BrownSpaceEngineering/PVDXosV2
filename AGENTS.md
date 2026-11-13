# PVDXos Repository Instructions

PVDXos is the real-time operating system for Brown Space Engineering's PVDX
satellite, running on an ATSAMD51P20A/SAMD51 Cortex-M4. The firmware is built
with the Arm GNU toolchain (`arm-none-eabi-*`) and runs on hardware such as the
Adafruit Grand Central development board.

## Architecture and hardware

- `bootloader/` is a two-stage bootloader; the layout is defined in
  `bootloader/src/image_format.h`. Stage 1 (`src/stage1.c`, flash
  `0x00000000`) jumps to the first of three stage-2 copies (`0x00002000`,
  `0x00006000`, `0x0000A000`, 16 KB each, CRC32 in the last 4 bytes) whose CRC
  is valid. Stage 2 (`src/stage2.c`, an ASF project in `bootloader/ASF`)
  copies MRAM (three chips holding the same copy; header at MRAM `0x000000`) into any
  8 KB flash block that doesn't match it, using the ASF flash HAL. When a
  debugger requests it (`make flash_to_mram`), it instead copies flash into
  MRAM on that one boot. It then
  leaves a boot report in backup RAM, and jumps to PVDXos. The block CRCs live
  in the metadata block at `0x0000E000`.
- `src/` is the application; `src/src_flash.ld` links it to run in place from
  flash at `0x0001E000`, above any `BOOTPROT` region (RAM holds only data, bss and stacks).
  PVDXos and the bootloader only use the bottom 128 KB of RAM: the top half holds
  the RAM ECC check bits once the RAM ECCDIS fuse (user row bit 39) is cleared, and
  `src/misc/ram_ecc/` reports ECC errors.
  `src/main.c` initializes Atmel Start hardware, logging, logs the bootloader's
  boot report, runs startup checks, tasks, and FreeRTOS.
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
  `ATSAMD51P20A`, SWD, and (for GDB) server port 2331. RTT diagnostics use the
  existing SEGGER RTT path.

## Build, test, and debug

Run commands from the repository root:

```bash
make
make dev
make clean all
make release
make test
make -C bootloader/src
make -C src dev
make clean
```

`make dev`, `make release`, and `make test` rebuild the bootloader and run
`scripts/create_flash_segment.py`, which assembles the stage 1 bootloader, the three
CRC-checked stage-2 bootloaders, the image metadata and PVDXos into `flash.bin`.
The bootloader copies MRAM into any flash block that doesn't match and never
writes to MRAM, so a build flashed with `flash.jlink` gets overwritten by the
golden copy. To make a build the golden copy, flash it with `make flash_to_mram`
(`flash_to_mram.jlink`), which leaves a request word in backup RAM that makes
the bootloader copy flash into MRAM on that one boot. `src/Makefile` passes
exactly one of `DEVBUILD`, `RELEASE`, or `UNITTEST`; do not combine these
manually. Tests are embedded firmware tests: `make test` calls `tests_run()` in
`src/main.c` before FreeRTOS, with results emitted through SEGGER RTT. Current
test functions cover SPP, linear algebra matrix multiplication, and CFDP
parsing. To build only some tests, pass their names, e.g.
`make test TESTS="spp cfdp"` (defines `TEST_SPP`/`TEST_CFDP`; see `TEST_NAMES`
in `src/Makefile` and `src/tests/test.h`); with no `TESTS`, all are built.
Builds fail if `PVDXos.bin` exceeds its 440 KB MRAM golden-copy slot or the
linker's flash or RAM region overflows.

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

`make flash_monkey` intentionally flips random bits in `flash.bin` (limit them
with `REGION=app` or `REGION=boot`) to exercise the bootloader's repair; rebuild
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
- Linker scripts and flash/MRAM offsets are part of the redundancy protocol.
  Change them in `bootloader/src/image_format.h` (which
  `scripts/image_format.py` parses for `create_flash_segment.py` and
  `gen_bootloaders.py`), and keep `src/src_flash.ld`,
  `bootloader/src/bootloader_flash.ld.template` and `NUM_STAGE2_COPIES` in
  `bootloader/src/Makefile` consistent with it.

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
`make connect_bl`, `gdb` directly or `python3 scripts/rtt_logs.py`. Use these wrappers instead (details in the `debug-pvdxos-hardware` skill):

- GDB server: `tools/jlink.sh start|stop|status`
- Firmware log (RTT channel 1) in the background: `tools/rtt.sh start`, `tools/rtt.sh tail 200`, `tools/rtt.sh stop`
- Inspect the running target (safe, read-only): `tools/gdb.sh "bt" "rtos_current" "faults" "info threads"`
- Catch a fault (board resets ~1 s after `fatal()`): `GDB_TIMEOUT=90 tools/gdb.sh wait_fault`
- Build/flash: see the `build-and-flash-pvdxos` skill. Flash with `flash.jlink`, which writes the whole
  `flash.bin`. Never GDB `load` `src/PVDXos.elf`: it leaves the metadata block stale, so at the next boot the
  bootloader sees flash as corrupt and rewrites it from the MRAM golden copy.

Rules:

- Reading registers, memory, backtraces and RTT logs is fine. Flashing, resetting, erasing, or writing
  memory/registers needs user approval.
- Read the RTT log first; use GDB snapshots to confirm.
- The hardware watchdog is always on (about 16 s); don't leave the core halted.

<!-- END debug-tooling -->
