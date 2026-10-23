#!/usr/bin/env python3
"""Bounded SWD/RTT capture that waits for the RAM-linked application to boot."""
import argparse
from pathlib import Path
import subprocess
import time

import pylink


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=90)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    root = Path(__file__).resolve().parents[1]
    symbols = subprocess.check_output(
        ["arm-none-eabi-nm", "-n", str(root / "src/PVDXos.elf")], text=True
    )
    address = next(int(line.split()[0], 16) for line in symbols.splitlines()
                   if line.split()[-1] == "_SEGGER_RTT")
    output = args.output if args.output.is_absolute() else root / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    started = False
    with output.open("w") as log, pylink.JLink() as link:
        link.open()
        link.set_tif(pylink.enums.JLinkInterfaces.SWD)
        link.connect("ATSAMD51P20A", speed=4000)
        if link.halted():
            raise RuntimeError("Target is halted; refusing to change execution state")
        print(f"Waiting for application VTOR and RTT at 0x{address:08x}", flush=True)
        deadline = time.monotonic() + args.seconds
        try:
            while time.monotonic() < deadline:
                app_running = link.memory_read32(0xE000ED08, 1)[0] == 0x20000000
                ready = app_running and bytes(link.memory_read8(address, 10)) == b"SEGGER RTT"
                if not ready:
                    if started:
                        link.rtt_stop()
                        started = False
                    time.sleep(0.02)
                    continue
                if not started:
                    link.rtt_start(block_address=address)
                    started = True
                    print("Application reached; capturing channel 1", flush=True)
                data = link.rtt_read(1, 4096)
                if data:
                    text = bytes(data).decode("ascii", errors="replace")
                    print(text, end="", flush=True)
                    log.write(text)
                    log.flush()
                time.sleep(0.01)
        finally:
            if started:
                link.rtt_stop()
        print(f"\nCapture complete; halted: {link.halted()}; log: {output}", flush=True)


if __name__ == "__main__":
    main()
