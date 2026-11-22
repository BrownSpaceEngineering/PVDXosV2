# PVDXos Repository Instructions

PVDXos is the real-time operating system for Brown Space Engineering's PVDX
satellite, running on an ATSAMD51P20A/SAMD51 Cortex-M4. The firmware is built
with the Arm GNU toolchain (`arm-none-eabi-*`) and runs on hardware such as the
Adafruit Grand Central development board.

## Architecture and hardware

- `bootloader/` is a two-stage bootloader; the layout is defined in
  `bootloader/src/image_format.h`. Flash (1 MB):
  - `0x00000` stage 1 (`src/preboot.c`, 8 KB, CRC32 in its last 4 bytes)
    jumps to the first of eight stage-2 copies (`0x02000` + n × `0x2000`,
    up to `0x12000`, 8 KB each, CRC32 in the last 4 bytes) whose CRC is valid.
    If none is, it jumps straight to PVDXos with no repair.
  - `0x80000` the OS image (second flash bank): an 8 KB header row
    (`image_header_t`: magic, app size, one CRC32 per 8 KB app block, header
    CRC) followed by PVDXos at `0x82000` (at most `0x7E000` bytes).

  Stage 2 (`src/bootloader.c`, an ASF project in `bootloader/ASF`) configures
  the hardware watchdog and MRAM, then checks the header row and every app
  block (CRC and flash ECC) against the golden copy in MRAM, rewriting any that
  differ. MRAM reads are a bitwise majority vote of the three chips, falling
  back to each chip on its own when the vote fails the CRC. When the refresh
  request word is set in backup RAM (`make flash_to_mram` or
  `tools/mram_refresh.sh request`), it first copies flash into MRAM instead,
  keeping the request until a copy completes. It leaves a boot record in the
  last 64 bytes of backup RAM (`src/boot_record.h`, `0x47001FC0`) and jumps
  to PVDXos.
- MRAM (three EM008LX chips, 1 MB each, written identically):
  `0x000000` golden copy of the OS image (header row + PVDXos, 512 KB),
  `0x080000` golden copy of stage 1 and the stage-2 copies (72 KB), and from
  `0x093000` PVDXos's own regions (`src/drivers/mram/mram_map.h`). PVDXos
  reaches MRAM only through `mram_map.h`, which range-checks each region and
  serializes tasks with a mutex.
- The flash, MRAM and watchdog drivers are shared:
  `bootloader/src/{flash,mram,watchdog}_driver.[ch]` are the sources, and
  `src/drivers/{flash,mram,watchdog}/` hold symlinks to them. PVDXos finds
  `image_format.h` and `boot_record.h` through `-idirafter bootloader/src`.
  The bootloader configures the watchdog; PVDXos only pets and kicks it.
- `src/` is the application; `src/src_flash.ld` links it to run in place from
  flash at `0x00082000` (RAM holds only data, bss and stacks). The SEGGER RTT
  control block is pinned to `0x20000000`, the same address stage 2 uses, and
  is not zeroed at startup, so `main()` calls `SEGGER_RTT_Init()` first.
  `src/main.c` initializes Atmel Start hardware and logging, logs the boot
  record, runs startup checks, tasks, and FreeRTOS. The reflash task
  (`src/tasks/reflash/`) checks stage 1 and the stage-2 copies at startup and
  every six hours, and rewrites any that fail their CRC or ECC from MRAM.
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
`scripts/create_flash_segment.py`, which assembles stage 1, the eight
CRC-checked stage-2 copies, the OS header row and PVDXos into a 1 MB `flash.bin`.
The bootloader copies MRAM into any flash block that doesn't match, and writes
MRAM only when asked to, so a build flashed with `flash.jlink` gets overwritten
by the golden copy. To make a build the golden copy, flash it with
`make flash_to_mram` (`flash_to_mram.jlink`), which leaves the refresh request
word in backup RAM so the bootloader copies flash into MRAM on the next boot
(`tools/mram_refresh.sh request|cancel|status` does the same without flashing,
and reports progress). `src/Makefile` passes
exactly one of `DEVBUILD`, `RELEASE`, or `UNITTEST`; do not combine these
manually. Tests are embedded firmware tests: `make test` calls `tests_run()` in
`src/main.c` before FreeRTOS, with results emitted through SEGGER RTT. Current
test functions cover SPP, linear algebra matrix multiplication, and CFDP
parsing. To build only some tests, pass their names, e.g.
`make test TESTS="spp cfdp"` (defines `TEST_SPP`/`TEST_CFDP`; see `TEST_NAMES`
in `src/Makefile` and `src/tests/test.h`); with no `TESTS`, the defaults are
built. `TESTS=mram` is a hardware MRAM test that overwrites (then restores) MRAM
and is never built by default.
Builds fail if `PVDXos.bin` exceeds its 504 KB flash slot (`FLASH_OS_APP_SIZE`)
or the linker's flash or RAM region overflows.

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
  `bootloader/src/bootloader_flash.ld.template`, `NUM_BOOTLOADERS` in
  `bootloader/src/Makefile` (must equal `FLASH_STAGE2_COUNT`) and
  `src/drivers/mram/mram_map.h` (PVDXos regions must start after the MRAM
  golden copies) consistent with it. Backup RAM's last 64 bytes belong to the
  boot record and refresh request (`bootloader/src/boot_record.h`); both linker
  scripts stop their `bkupram` region before them.

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
  `flash.bin`. Never GDB `load` `src/PVDXos.elf`: it leaves the header row's block CRCs at `0x80000` stale, so
  at the next boot the bootloader sees flash as corrupt and rewrites it from the MRAM golden copy.
- `tools/mram_refresh.sh status` only reads; `request` and `cancel` write backup RAM (and `request` resets the
  board), so they need user approval like flashing.

Rules:

- Reading registers, memory, backtraces and RTT logs is fine. Flashing, resetting, erasing, or writing
  memory/registers needs user approval.
- Read the RTT log first; use GDB snapshots to confirm.
- The hardware watchdog is always on (about 16 s); don't leave the core halted.

<!-- END debug-tooling -->
