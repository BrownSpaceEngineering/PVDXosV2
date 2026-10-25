"""Read diagnostic logs and framed JPEGs through one J-Link RTT connection."""

import argparse
import datetime
import math
from pathlib import Path
import time

from camera_stream import ImageDecoder

ROOT = Path(__file__).resolve().parents[1]


def save_image(image, directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d_%H-%M-%S_%f")
    path = directory / f"{stamp}_frame-{image.frame_id}_{image.width}x{image.height}.jpg"
    # Only completed frames reach this function; never replace an existing capture.
    with path.open("xb") as output:
        output.write(image.jpeg)
    return path


def capture_channels(jlink, logfile=None, images_dir=ROOT / "logs" / "images", duration=None):
    """Drain both channels until interrupted or the optional duration expires."""
    decoder = ImageDecoder()
    deadline = None if duration is None else time.monotonic() + duration
    while deadline is None or time.monotonic() < deadline:
        # Prioritize the small image ring so a busy text channel cannot starve it.
        image_data = bytes(jlink.rtt_read(2, 4096))
        for image in decoder.feed(image_data):
            path = save_image(image, images_dir)
            print(f"[camera] Saved {path} ({len(image.jpeg)} bytes)", flush=True)
        log_data = bytes(jlink.rtt_read(1, 2048))
        if log_data:
            text = log_data.decode("ascii", errors="replace")
            print(text, end="", flush=True)
            if logfile is not None:
                logfile.write(text)
                logfile.flush()
        if not image_data and not log_data:
            time.sleep(0.005)
    return decoder


def open_rtt_channels(logfile=None, images_dir=ROOT / "logs" / "images", duration=None):
    import pylink  # pip install pylink-square; not required for decoding/tests.

    jlink = pylink.JLink()
    jlink.open()
    try:
        jlink.connect(chip_name="ATSAMD51P20A")
        jlink.rtt_start()
        try:
            return capture_channels(jlink, logfile, images_dir, duration)
        finally:
            jlink.rtt_stop()
    finally:
        jlink.close()


def positive_seconds(value):
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError("duration must be a positive, finite number of seconds")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration", type=positive_seconds,
                        help="stop after this many seconds (otherwise run until interrupted)")
    parser.add_argument("--images-dir", type=Path, default=ROOT / "logs" / "images",
                        help="directory for validated JPEGs (default: logs/images)")
    args = parser.parse_args()
    logs_path = ROOT / "logs"
    logs_path.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S_%f")
    try:
        with (logs_path / f"{stamp}.log").open("x", encoding="utf-8") as logfile:
            open_rtt_channels(logfile, args.images_dir, args.duration)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
