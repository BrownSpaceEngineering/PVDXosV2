# Scripts

The scripts folder is comprised of a collection of scripts that are used to automate various tasks or interact with PVDXos in some way. They are _never_ linked together with the main application (i.e. are not included in PVDXos itself).

## MRAM driver host regressions

Run `python3 scripts/test_mram_driver.py` from the repository root. Requires Python 3
and `gcc`; no hardware or Python packages are needed. The tests compile the shared
bootloader/application MRAM driver against a mocked SPI device in a temporary
folder. They cover block-protection clearing, status-bit preservation, write-enable
verification, bounded completion polling, transfer failures, and error flags.

The driver clears nonvolatile block-protection bits only when needed, preserving
status-register write protection and top/bottom selection. Public array APIs remain
`void`: writes still attempt every replica; an incomplete read returns before
majority voting or repair, allowing the bootloader's checksum/fallback path to run.

## MRAM hardware tests (opt-in)

From the repository root, build with `make -j8 test TESTS=mram`, then flash the
resulting `flash.bin` using the repository's J-Link flashing workflow and inspect
SEGGER RTT test results. MRAM tests are excluded when `TESTS` is omitted; selecting
`mram` runs only MRAM tests.

**Warning:** These tests temporarily change nonvolatile block-protection settings
and write/restore test arrays on real MRAM. Use expendable hardware/data: an
interrupted test may leave protection settings or array contents changed. This
branch's `WRITE_FLASH_TO_MRAM` boot path already overwrites the first 512 KiB of
MRAM before the tests run.

The test uses the shared production driver for each chip's ID, BP2/BP3 clearing
and status preservation, a stable original snapshot, five 512-byte cross-page
patterns, public replicated writes, injected-bit majority voting/repair, and
restoration verified by three reads. The test region starts at `0x0800F0`, outside
the boot mirror. Restoration is best-effort on failure or interruption.

At the default 50 kHz SPI clock, boot mirroring and application loading take several
minutes. Capture without halting/resetting the target using a Python environment
with `pylink-square` installed:

```sh
/home/yi/dev/venv/bin/python3 tools/mram_hardware_capture.py \
  --seconds 600 --output logs/mram-hardware.log
```

This bounded tool explicitly uses SWD, waits for the application's VTOR and RTT
control block, and captures log channel 1. Start it immediately after flashing.
Look for `MRAM HW END: PASS` **and** `test results: 43/43 passed`; successful SPI
transfers alone do not prove device response. Startup `MRAM_ID: 0` on this branch
comes from a separate, unimplemented device-check callback, not this test.

### Destructive post-reflow factory initialization (explicit opt-in only)

`src/tests/mram_factory_init.c` implements EST3000 Rev2.1 initialization with
JESD252 reset, verified DFIM entry/exit, status/NV configuration, OTP writes, and
four full 1 MiB array write/read-verification passes per chip (`00/FF/00/FF`).
It deliberately uses explicit writes instead of the reference's conflicting
bulk-erase polarity. It checks transfer results, readiness/error flags and readback.

**This destroys all three chips' array, OTP and configuration contents.** Run only
on authorized, expendable data. A factory-enabled image repeats the operation on
every boot. The macro is not enabled by any normal build target:

```sh
make -C src clean
CFLAGS='-DCONF_SERCOM_0_SPI_BAUD=2000000 -DMRAM_FACTORY_INIT' make -j8 test TESTS=mram
# Flash using flash.jlink; capture continuously until all three chips complete.
# Do not reset during initialization. Then remove the factory image:
make -C src clean
make -j8 test TESTS=mram
# Flash again and rerun the normal hardware test.
```

Clean the application when changing `CFLAGS`: the build does not reliably detect
flag-only changes. Never leave the factory image flashed for normal use.

### Board verification — 2026-10-21

On the connected PVDX SAMD51 board, branch `mram-driver-audit` with these uncommitted
changes:

- All three chips completed verified factory initialization at 2 MHz, including
  all four full-array passes. Completion records are in local ignored logs
  `logs/mram-audit-factory-verification{,-cont,-final}.log`.
- Rebuilt cleanly **without** `MRAM_FACTORY_INIT`, flashed and verified with
  J-Link, then tested at the default **50 kHz** (live SERCOM0 BAUD `0x77`).
- All three IDs were `6B BB 14`; all patterns, BP regressions, majority repair and
  repeated original-data restoration passed: **43/43 assertions**, `MRAM HW END:
PASS`. Full record: `logs/mram-audit-default-50khz-verification.log`.
- The earlier MRAM2 single-bit restoration failure did not recur after factory
  initialization; that observation does not establish its original cause.
- Final flashed firmware is the non-factory, opt-in MRAM test build; factory
  initializer symbol is absent from its ELF. The board continued into FreeRTOS.
- Host regressions passed **14/14**. Boot fallback failure scenarios are mocked
  host tests, not hardware fault-injection coverage.

Public array APIs still return `void`, so callers do not receive transfer status.
An incomplete read returns before voting/repairing its incomplete page, but callers
must not assume a valid checksum proves a fresh successful read: RAM can already
contain a valid older image. Explicit read-status propagation remains future work.
