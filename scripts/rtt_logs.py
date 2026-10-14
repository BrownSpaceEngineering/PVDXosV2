import platform
import os
import datetime
import re
import tempfile
import time


def open_rtt_channels(logfile = None):
    import pylink # pip install pylink-square

    jlink = pylink.JLink()
    jlink.open()
    jlink.connect(chip_name='ATSAMD51P20A')
    jlink.rtt_start()

    while True:
        buf1 = jlink.rtt_read(1, 2048)

        if buf1:
            text1 = bytes(buf1).decode('ascii', errors='replace')
            print(text1)
            # Write to a log file if specified
            if logfile is not None:
                logfile.write(text1)


class CaptureError(RuntimeError):
    """Frame alignment was lost; reconnect before receiving more images."""


class ArducamReceiver:
    """Receive the driver's two-channel stream without importing pylink.

    Log markers have ANSI/whitespace delimiters, not necessarily newlines.
    Channel 2 is unframed: after an abort, missing marker, or short transfer,
    fail closed instead of potentially joining bytes from different captures.
    Start before capture with empty RTT buffers and an intact log stream;
    there are no frame IDs to recover alignment after lost logs or stale data.
    Unfinished files remain .part; only a full frame plus IMG_END is published.
    """

    # Match the driver's OV2640_MAX_FIFO_SIZE bound. A delimiter is essential:
    # a read ending in "len=12" might be the beginning of "len=12345".
    MAX_FRAME_SIZE = 0x5FFFF
    MARKER = re.compile(
        r"IMG_BEGIN len=([0-9]{1,6})(?=\D)|IMG_END(?=\W)|"
        r"arducam: Image transfer aborted"
    )

    def __init__(self, read, logs_dir, timeout=5.0, clock=time.monotonic):
        self.read = read
        self.logs_dir = logs_dir
        self.timeout = timeout
        self.clock = clock
        self.pending = ""
        self.file = None
        self.expected = 0
        self.received = 0
        self.progress = clock()

    def feed_logs(self, text):
        self.pending += text
        while True:
            marker = self.MARKER.search(self.pending)
            if marker is None:
                # Retain split markers, but not an unbounded copy of all logs.
                self.pending = self.pending[-128:]
                return
            self.pending = self.pending[marker.end():]
            if marker.group(1) is not None:
                if self.file is not None:
                    self.fail("New IMG_BEGIN before IMG_END")
                length = int(marker.group(1))
                if not 0 < length < self.MAX_FRAME_SIZE:
                    self.fail(f"Invalid frame length: {length}")
                os.makedirs(self.logs_dir, exist_ok=True)
                stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
                self.file = tempfile.NamedTemporaryFile(
                    mode="wb", prefix=f"{stamp}_capture_", suffix=".jpg.part",
                    dir=self.logs_dir, delete=False,
                )
                self.expected = length
                self.received = 0
                self.progress = self.clock()
                print(f"[RTT] Receiving {length} bytes into {self.file.name}")
            elif marker.group(0).startswith("arducam:"):
                self.fail("Firmware aborted image transfer")
            elif self.file is None:
                self.fail("IMG_END without IMG_BEGIN (receiver attached mid-frame)")
            else:
                # IMG_END is queued after all binary writes, but channel 2 may
                # still contain most of the frame. Drain it before deciding.
                self.drain()
                if self.received != self.expected:
                    self.fail(f"Truncated image: {self.received}/{self.expected} bytes")
                path = self.file.name
                self.file.close()
                self.file = None
                os.replace(path, path[:-5])  # Remove only the .part suffix.
                print(f"[RTT] Image saved: {path[:-5]} ({self.received} bytes)")

    def drain(self):
        # Never consume bytes belonging to the next frame, even if both are
        # already queued on channel 2. The length also bounds this loop.
        while self.file is not None and self.received < self.expected:
            data = self.read(2, min(8192, self.expected - self.received))
            if not data:
                break
            self.file.write(bytes(data))
            self.received += len(data)
            self.progress = self.clock()

    def poll(self):
        self.drain()
        if self.file is not None and self.clock() - self.progress >= self.timeout:
            self.fail(f"Image stalled or IMG_END missing: {self.received}/{self.expected} bytes")

    def close(self):
        if self.file is not None:
            path = self.file.name
            self.file.close()
            self.file = None
            print(f"[RTT] Incomplete image retained: {path} ({self.received}/{self.expected} bytes)")

    def fail(self, message):
        self.close()
        raise CaptureError(f"{message}; frame alignment lost, restart the receiver before a new capture")


def open_rtt_channels_arducam_debug(logfile = None):
    """Log channel 1 and save complete channel-2 captures; stop on lost alignment.

    Start before triggering capture_rtt(). Requires pip install pylink-square.
    After an error, stop captures and clear stale RTT data before reconnecting.
    """
    import pylink # pip install pylink-square

    jlink = pylink.JLink()
    receiver = ArducamReceiver(
        jlink.rtt_read, os.path.normpath(os.path.join(os.path.dirname(__file__), '..', 'logs')),
    )
    try:
        jlink.open()
        jlink.connect(chip_name='ATSAMD51P20A')
        jlink.rtt_start()
        while True:
            buf1 = jlink.rtt_read(1, 4096)
            if buf1:
                text1 = bytes(buf1).decode('ascii', errors='replace')
                print(text1, end="")
                if logfile is not None:
                    logfile.write(text1)
                receiver.feed_logs(text1)
            receiver.poll()
            if not buf1:
                time.sleep(0.001)
    finally:
        receiver.close()
        jlink.close()


def main():
    # Sanity check to make sure this is not in WSL
    # if "microsoft" in platform.uname().release:
    #     print("This code cannot be executed in a WSL environment. Run natively on Windows instead!")
    #     exit(1)

    #Check if the ./logs directory exists
    current_script_dir = os.path.dirname(__file__)
    logs_path = os.path.join(current_script_dir, '..', 'logs')
    logs_path = os.path.normpath(logs_path)

    #If the logs directory does not exist, create it
    if not os.path.exists(logs_path):
        os.makedirs(logs_path)

    # Create a log file in the logs directory with the current date and time
    current_time = datetime.datetime.now()
    log_filename = current_time.strftime("%Y-%m-%d_%H-%M-%S.log")
    log_file_path = os.path.join(logs_path, log_filename)
    
    with open (log_file_path, "w") as file:
        # open_rtt_channels(file)
        open_rtt_channels_arducam_debug(file)


if __name__ == "__main__":
    main()
