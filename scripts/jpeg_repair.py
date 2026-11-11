#!/usr/bin/env python3
"""Rebuild a decodable JPEG from a camera image that arrived with pieces missing.

The flight software puts a restart marker after every MCU row (src/drivers/arducam/jpeg_restart.c), so each row
band decodes on its own. Given the received file and the byte ranges that never arrived, this keeps every band that
arrived whole, leaves the others empty (decoders fill them with grey), and puts back the restart markers lost with the
missing bytes, so that every band after a gap still lands on the right rows.

Restart markers only count 0 to 7, so a gap's band count is worked out from the markers on either side of it, using
the gap's size to choose between counts that differ by 8.

The header (everything up to the start of the scan) has to have arrived intact.

Usage: jpeg_repair.py received.jpg repaired.jpg --missing START:END [START:END ...]   (byte offsets, END exclusive)
"""

import argparse
import sys


def parse_header(data, missing):
    """Returns (scan start offset, restart interval in MCUs, total MCUs)."""
    if data[:2] != b"\xff\xd8":
        raise ValueError("no SOI")
    pos, interval, width, height, sampling = 2, None, None, None, []
    while True:
        if data[pos] != 0xFF:
            raise ValueError(f"no marker at {pos}")
        marker, length = data[pos + 1], int.from_bytes(data[pos + 2:pos + 4], "big")
        body = data[pos + 4:pos + 2 + length]
        if marker == 0xDD:
            interval = int.from_bytes(body[:2], "big")
        elif marker in (0xC0, 0xC1):
            height, width = int.from_bytes(body[1:3], "big"), int.from_bytes(body[3:5], "big")
            sampling = [(body[7 + 3 * i] >> 4, body[7 + 3 * i] & 15) for i in range(body[5])]
        pos += 2 + length
        if marker == 0xDA:
            break
    if any(start < pos for start, _ in missing):
        raise ValueError("part of the header is missing")
    if not interval or not width:
        raise ValueError("no restart interval or frame header")
    h_max = max(h for h, _ in sampling) if len(sampling) > 1 else 1
    v_max = max(v for _, v in sampling) if len(sampling) > 1 else 1
    mcus = -(-width // (8 * h_max)) * -(-height // (8 * v_max))
    return pos, interval, mcus


def repair(data, missing):
    missing = sorted(missing)
    received = bytearray(b"\x01") * len(data)
    for start, end in missing:
        received[start:end] = bytes(end - start)
    scan, interval, mcus = parse_header(data, missing)
    bands = -(-mcus // interval)

    # Restart markers whose two bytes both arrived, in order: (offset, number mod 8)
    markers = [(p, data[p + 1] - 0xD0) for p in range(scan, len(data) - 1)
               if data[p] == 0xFF and 0xD0 <= data[p + 1] <= 0xD7 and received[p] and received[p + 1]]

    # Give each marker the band that starts after it. A marker after band k is RST(k mod 8).
    avg_band = (len(data) - scan) / bands
    band_starts = {0: scan}  # band -> offset of its first entropy-coded byte
    band, prev_end = 0, scan
    for pos, number in markers:
        gap = sum(min(end, pos) - max(start, prev_end) for start, end in missing if start < pos and end > prev_end)
        lost = 0
        if gap:
            # Bands lost in the gap: the count that fits the marker number, closest to what the gap's size suggests
            first = (number - band) % 8
            estimate = gap / avg_band
            lost = min((first + 8 * k for k in range(int(estimate // 8) + 2)), key=lambda n: abs(n - estimate))
        elif number != band % 8:
            raise ValueError(f"restart marker at {pos} is RST{number}, expected RST{band % 8}")
        band += lost + 1
        if band >= bands:
            break
        band_starts[band] = pos + 2
        prev_end = pos + 2

    end_of_scan = data.rfind(b"\xff\xd9")
    end_of_scan = end_of_scan if end_of_scan > scan and received[end_of_scan] else len(data)
    out = bytearray(data[:scan])
    kept = 0
    for b in range(bands):
        start = band_starts.get(b)
        if start is not None:
            # The band runs to the next marker that arrived, if it is the next band's; otherwise it can't be trusted
            nxt = band_starts.get(b + 1, None)
            stop = nxt - 2 if nxt is not None else (end_of_scan if b == bands - 1 else None)
            if stop is not None and all(received[start:stop]):
                out += data[start:stop]
                kept += 1
        if b < bands - 1:
            out += bytes([0xFF, 0xD0 + b % 8])
    out += b"\xff\xd9"
    return bytes(out), kept, bands


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("received")
    parser.add_argument("repaired")
    parser.add_argument("--missing", nargs="*", default=[], metavar="START:END",
                        help="byte ranges that never arrived, END exclusive")
    args = parser.parse_args()
    data = open(args.received, "rb").read()
    missing = [tuple(int(x) for x in r.split(":")) for r in args.missing]
    out, kept, bands = repair(data, missing)
    open(args.repaired, "wb").write(out)
    print(f"{kept} of {bands} row bands intact")


if __name__ == "__main__":
    sys.exit(main())
