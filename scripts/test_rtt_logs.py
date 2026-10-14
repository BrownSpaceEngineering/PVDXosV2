"""Hardware-free receiver tests: python3 -B -m unittest discover -s scripts -p test_rtt_logs.py -v."""

import contextlib
import io
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest import mock

import rtt_logs


RESET = "\x1b[0m"


def begin(length):
    return f"\x1b[1;37m[INFO|arducam_driver.c:219]: IMG_BEGIN len={length}{RESET}"


END = f"\x1b[1;37m[INFO|arducam_driver.c:257]: IMG_END{RESET}"
ABORT = ("\x1b[1;31m[WARNING|arducam_driver.c:254]: "
         f"arducam: Image transfer aborted (SPI error or RTT timeout)\n{RESET}")


class BinaryChannel:
    def __init__(self, data=b"", chunk_size=8192):
        self.data = bytearray(data)
        self.chunk_size = chunk_size
        self.requests = []

    def read(self, channel, size):
        self.assert_channel(channel)
        self.requests.append(size)
        size = min(size, self.chunk_size)
        result = self.data[:size]
        del self.data[:size]
        return list(result)  # pylink returns a list, not bytes.

    @staticmethod
    def assert_channel(channel):
        if channel != 2:
            raise AssertionError(f"Unexpected channel {channel}")


class ReceiverTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        output = contextlib.redirect_stdout(io.StringIO())
        output.__enter__()
        self.addCleanup(output.__exit__, None, None, None)
        self.now = 0.0

    def receiver(self, channel):
        receiver = rtt_logs.ArducamReceiver(
            channel.read, self.directory, clock=lambda: self.now,
        )
        self.addCleanup(receiver.close)
        return receiver

    def images(self):
        return sorted(self.directory.glob("*.jpg"))

    def parts(self):
        return sorted(self.directory.glob("*.part"))

    def test_every_marker_split_including_length_and_ansi_reset(self):
        # No newlines: these markers match the current logging macros.
        logs = begin(123) + END
        for split in range(len(logs) + 1):
            with self.subTest(split=split):
                channel = BinaryChannel(b"x" * 123, chunk_size=7)
                receiver = self.receiver(channel)
                receiver.feed_logs(logs[:split])
                receiver.poll()
                receiver.feed_logs(logs[split:])
                receiver.poll()
                images = self.images()
                self.assertEqual(len(images), 1)
                self.assertEqual(images[0].read_bytes(), b"x" * 123)
                self.assertEqual(self.parts(), [])
                images[0].unlink()

    def test_markers_arriving_one_character_at_a_time(self):
        receiver = self.receiver(BinaryChannel(b"firstSECOND"))
        logs = begin(5) + END + begin(6) + END
        for character in logs:
            receiver.feed_logs(character)
            receiver.poll()
        self.assertCountEqual(
            [p.read_bytes() for p in self.images()], [b"first", b"SECOND"],
        )

    def test_partial_length_is_not_parsed_early(self):
        receiver = self.receiver(BinaryChannel(b"x" * 123))
        receiver.feed_logs("IMG_BEGIN len=12")
        receiver.poll()
        self.assertIsNone(receiver.file)
        receiver.feed_logs("3" + RESET + END)
        self.assertEqual(self.images()[0].read_bytes(), b"x" * 123)

    def test_end_drains_large_frame_in_multiple_reads(self):
        payload = b"\xff\xd8" + bytes(range(256)) * 200 + b"\xff\xd9"
        channel = BinaryChannel(payload, chunk_size=997)
        receiver = self.receiver(channel)
        receiver.feed_logs(begin(len(payload)) + END)
        self.assertEqual(self.images()[0].read_bytes(), payload)
        self.assertGreater(len(channel.requests), 2)
        self.assertLessEqual(max(channel.requests), 8192)

    def test_two_queued_images_remain_separate_with_unique_names(self):
        first, second = b"\xff\xd8one\xff\xd9", b"\xff\xd8two more\xff\xd9"
        channel = BinaryChannel(first + second)
        receiver = self.receiver(channel)
        receiver.feed_logs(begin(len(first)) + END + begin(len(second)) + END)
        self.assertCountEqual([p.read_bytes() for p in self.images()], [first, second])
        self.assertEqual(channel.requests, [len(first), len(second)])
        self.assertFalse(channel.data)

    def test_full_length_waits_for_end_and_does_not_consume_next_image(self):
        channel = BinaryChannel(b"firstSECOND")
        receiver = self.receiver(channel)
        receiver.feed_logs(begin(5))
        receiver.poll()
        receiver.poll()
        self.assertEqual(channel.data, b"SECOND")
        self.assertEqual(self.images(), [])
        self.assertEqual(len(self.parts()), 1)
        receiver.feed_logs(END)
        self.assertEqual(self.images()[0].read_bytes(), b"first")

    def test_abort_split_at_every_position_never_publishes_full_payload(self):
        for split in range(len(ABORT) + 1):
            with self.subTest(split=split):
                receiver = self.receiver(BinaryChannel(b"dataNEXT"))
                receiver.feed_logs(begin(4))
                receiver.poll()
                with self.assertRaisesRegex(rtt_logs.CaptureError, "aborted"):
                    receiver.feed_logs(ABORT[:split])
                    receiver.feed_logs(ABORT[split:] + END + begin(4) + END)
                self.assertEqual(self.images(), [])
                self.assertIsNone(receiver.file)
                parts = self.parts()
                self.assertEqual(len(parts), 1)
                self.assertEqual(parts[0].read_bytes(), b"data")
                parts[0].unlink()

    def test_abort_before_end_does_not_read_queued_next_frame(self):
        channel = BinaryChannel(b"partialNEXT_IMAGE")
        receiver = self.receiver(channel)
        receiver.feed_logs(begin(100))
        with self.assertRaisesRegex(rtt_logs.CaptureError, "aborted"):
            receiver.feed_logs(ABORT + END + begin(10) + END)
        self.assertEqual(channel.data, b"partialNEXT_IMAGE")
        self.assertEqual(self.images(), [])
        self.assertEqual(self.parts()[0].read_bytes(), b"")

    def test_truncation_closes_partial_instead_of_waiting_for_next_image(self):
        receiver = self.receiver(BinaryChannel(b"short"))
        with self.assertRaisesRegex(rtt_logs.CaptureError, "Truncated image: 5/20"):
            receiver.feed_logs(begin(20) + END + begin(30))
        self.assertEqual(self.images(), [])
        self.assertEqual(self.parts()[0].read_bytes(), b"short")
        self.assertIsNone(receiver.file)

    def test_stall_and_missing_end_have_bounded_timeout(self):
        for payload in (b"", b"data"):
            with self.subTest(payload=payload):
                self.now = 0.0
                receiver = self.receiver(BinaryChannel(payload))
                receiver.feed_logs(begin(4))
                receiver.poll()
                self.now = 5.0
                with self.assertRaisesRegex(rtt_logs.CaptureError, "stalled or IMG_END missing"):
                    receiver.poll()
                self.assertEqual(self.images(), [])
                self.assertIsNone(receiver.file)
                for path in self.parts():
                    path.unlink()

    def test_unexpected_markers_and_invalid_lengths_fail_closed(self):
        for logs in (END, begin(0), begin(0x5FFFF), begin(2) + begin(3)):
            with self.subTest(logs=logs):
                receiver = self.receiver(BinaryChannel())
                with self.assertRaises(rtt_logs.CaptureError):
                    receiver.feed_logs(logs)
                self.assertEqual(self.images(), [])

    def test_interruption_retains_partial_and_closes_file(self):
        receiver = self.receiver(BinaryChannel(b"some"))
        receiver.feed_logs(begin(10))
        receiver.poll()
        handle = receiver.file
        receiver.close()
        self.assertTrue(handle.closed)
        self.assertEqual(self.parts()[0].read_bytes(), b"some")
        receiver.close()  # Cleanup is idempotent.

    def test_unrelated_logs_do_not_grow_parser_buffer(self):
        receiver = self.receiver(BinaryChannel())
        for _ in range(100):
            receiver.feed_logs("ordinary log message\n" * 200)
        self.assertLessEqual(len(receiver.pending), 128)
        with self.assertRaisesRegex(rtt_logs.CaptureError, "Truncated image"):
            receiver.feed_logs(begin(1) + END)

    def test_live_wrapper_preserves_logs_and_closes_probe_on_interrupt(self):
        class FakeLink:
            closed = False

            def open(self):
                pass

            def connect(self, chip_name):
                if chip_name != "ATSAMD51P20A":
                    raise AssertionError(chip_name)

            def rtt_start(self):
                pass

            def rtt_read(self, channel, size):
                if not hasattr(self, "read_once"):
                    self.read_once = True
                    return list(b"ordinary logs\n")
                raise KeyboardInterrupt

            def close(self):
                self.closed = True

        probe = FakeLink()
        fake_pylink = types.SimpleNamespace(JLink=lambda: probe)
        logfile = io.StringIO()
        with mock.patch.dict(sys.modules, {"pylink": fake_pylink}):
            with self.assertRaises(KeyboardInterrupt):
                rtt_logs.open_rtt_channels_arducam_debug(logfile)
        self.assertEqual(logfile.getvalue(), "ordinary logs\n")
        self.assertTrue(probe.closed)


if __name__ == "__main__":
    unittest.main()
