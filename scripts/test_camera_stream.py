#!/usr/bin/env python3
"""Hardware-free PVIM decoder, RTT capture, and C/Python protocol regressions.

Run: python3 -B scripts/test_camera_stream.py -v
Only stdlib modules are required; cross-language tests additionally need the
host C compiler used by test_arducam_driver.py. Generated files stay in temporary
directories. No J-Link, pylink, firmware build, or hardware access is involved.
"""

import base64
import contextlib
import io
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock
import zlib

import camera_stream as stream
import rtt_logs
import test_arducam_driver as driver_tests


# A real baseline grayscale JPEG, not just synthetic SOF metadata. Generated once
# with Pillow: Image.new("L", (1600, 1200), 128).save(..., "JPEG", optimize=True).
# Pixel decoding was verified when creating the fixture; tests only require the
# decoder's documented SOF validation, not a third-party pixel decoder. Compress
# the repetitive entropy data to keep this legitimate multichunk fixture small.
JPEG = zlib.decompress(base64.b64decode(
    "eJztzk0KwjAUBOAX0CS1EatmY0ltUaEKpWgNqKAoCAU9rUdw4SH82XiT2C7VK8w3u3nw"
    "GHd3bwrO5akkxohYFXIvOpLkXPCmFEJ4nmwp3Va+rwa9fkdHYTyMQmOSSZ4mo2xszHQ1"
    "y+YLa22cbnbrYpsvbeGu5MvGhR8Y65K7ka4/f6vb4L99kqyn7H8PAAAAAAAAAAAAAAAAAABQ"
    "cY8PT88Y4g=="
))
WIDTH, HEIGHT = 1600, 1200


def packet(kind, payload, *, frame_id=1, offset=0, total=len(JPEG),
           version=1, size=None, payload_crc=None):
    """Build wire bytes independently of the decoder's struct/constants."""
    header = struct.pack(
        "<4sBBHIIII", b"PVIM", version, kind,
        len(payload) if size is None else size, frame_id, offset, total,
        zlib.crc32(payload) if payload_crc is None else payload_crc,
    )
    return header + struct.pack("<I", zlib.crc32(header)) + payload


def frame_packets(frame_id=1, jpeg=JPEG, dimensions=(WIDTH, HEIGHT), chunk_size=2000):
    common = {"frame_id": frame_id, "total": len(jpeg)}
    return [packet(1, struct.pack("<HH", *dimensions), **common)] + [
        packet(2, jpeg[offset:offset + chunk_size], offset=offset, **common)
        for offset in range(0, len(jpeg), chunk_size)
    ] + [packet(3, struct.pack("<I", zlib.crc32(jpeg)), offset=len(jpeg), **common)]


def frame_bytes(**kwargs):
    return b"".join(frame_packets(**kwargs))


def expected_image(frame_id=1, jpeg=JPEG):
    return stream.Image(frame_id, WIDTH, HEIGHT, jpeg)


class ImageDecoderTests(unittest.TestCase):
    def assert_recovers(self, broken, *, discarded=True):
        decoder = stream.ImageDecoder()
        self.assertEqual(decoder.feed(broken), [])
        self.assertEqual(decoder.feed(frame_bytes(frame_id=99)), [expected_image(99)])
        self.assertIsNone(decoder.active)
        self.assertEqual(decoder.buffer, b"")
        if discarded:
            self.assertGreaterEqual(decoder.discarded_frames, 1)
        return decoder

    def test_fixture_and_wire_contract(self):
        self.assertEqual(len(JPEG), 7658)
        self.assertEqual(stream.jpeg_dimensions(JPEG), (WIDTH, HEIGHT))
        self.assertEqual(stream.HEADER.size, 28)
        self.assertEqual(stream.MAX_PAYLOAD, 4067)
        self.assertEqual(stream.MAX_IMAGE_SIZE, 0x5FFFF - 1)
        self.assertEqual((stream.BEGIN, stream.DATA, stream.END, stream.ABORT), (1, 2, 3, 4))
        self.assertEqual(stream.ImageDecoder().feed(frame_bytes()), [expected_image()])

    def test_header_and_payload_fragmented_at_every_stream_split(self):
        wire = frame_bytes()
        for split in range(len(wire) + 1):
            with self.subTest(split=split):
                decoder = stream.ImageDecoder()
                images = decoder.feed(wire[:split]) + decoder.feed(wire[split:])
                self.assertEqual(images, [expected_image()])
                self.assertIsNone(decoder.active)
                self.assertEqual(decoder.buffer, b"")

    def test_bytewise_including_magic_header_and_payload(self):
        decoder = stream.ImageDecoder()
        wire = frame_bytes()
        for byte in wire[:-1]:
            self.assertEqual(decoder.feed(bytes([byte])), [])
        self.assertEqual(decoder.feed(wire[-1:]), [expected_image()])
        self.assertEqual(decoder.feed(b""), [])

    def test_multiple_frames_in_same_feed(self):
        decoder = stream.ImageDecoder()
        self.assertEqual(decoder.feed(frame_bytes() + frame_bytes(frame_id=2)),
                         [expected_image(), expected_image(2)])
        self.assertEqual((decoder.bad_packets, decoder.discarded_frames), (0, 0))

    def test_garbage_false_magic_and_split_magic_recover(self):
        decoder = stream.ImageDecoder()
        garbage = b"noisePVInot-magicPVIx" + b"PVIM" + bytes(40)
        self.assertEqual(decoder.feed(garbage + b"PVI"), [])
        self.assertLessEqual(len(decoder.buffer), 3)
        self.assertEqual(decoder.feed(frame_bytes()[3:]), [expected_image()])
        self.assertGreater(decoder.bad_packets, 0)

    def test_bad_version_header_crc_and_payload_crc_discard_active_frame(self):
        packets = frame_packets()
        wrong_header_crc = bytearray(packets[1])
        wrong_header_crc[24] ^= 1
        wrong_payload_crc = bytearray(packets[1])
        wrong_payload_crc[-1] ^= 1
        invalid = {
            "version": packet(2, JPEG[:2000], version=2),
            "header_crc": bytes(wrong_header_crc),
            "payload_crc": bytes(wrong_payload_crc),
        }
        for name, bad in invalid.items():
            with self.subTest(name=name):
                decoder = self.assert_recovers(packets[0] + bad + b"".join(packets[2:]))
                self.assertGreater(decoder.bad_packets, 0)

    def test_invalid_packet_lengths_and_shapes_rejected_without_waiting(self):
        cases = [
            {"kind": 1, "size": 0}, {"kind": 1, "size": 5},
            {"kind": 1, "size": 4, "offset": 1},
            {"kind": 2, "size": 0}, {"kind": 2, "size": 4068},
            {"kind": 2, "size": 65535},
            {"kind": 2, "size": 4, "offset": len(JPEG) - 3},
            {"kind": 2, "size": 4, "offset": 0xFFFFFFFF},
            {"kind": 3, "size": 3, "offset": len(JPEG)},
            {"kind": 3, "size": 4, "offset": len(JPEG) - 1},
            {"kind": 4, "size": 5},
            {"kind": 4, "size": 4, "offset": len(JPEG) + 1},
            {"kind": 5, "size": 4},
            {"kind": 1, "size": 4, "total": 0},
            {"kind": 1, "size": 4, "total": 0x5FFFF},
            {"kind": 1, "size": 4, "total": 0xFFFFFFFF},
        ]
        for fields in cases:
            with self.subTest(fields=fields):
                # No declared payload supplied: invalid headers must not cause a
                # wait for a bogus 64 KiB/4 GiB allocation or consume the next BEGIN.
                bad_header = packet(payload=b"", **fields)
                decoder = self.assert_recovers(frame_packets()[0] + bad_header)
                self.assertGreater(decoder.bad_packets, 0)

    def test_memory_retained_is_bounded_under_garbage_and_partial_packets(self):
        decoder = stream.ImageDecoder()
        for _ in range(128):
            self.assertEqual(decoder.feed(b"x" * 8192 + b"PVI"), [])
            self.assertLessEqual(len(decoder.buffer), 3)
        partial = packet(2, b"x" * 4067)
        self.assertEqual(decoder.feed(partial[:-1]), [])
        self.assertLessEqual(len(decoder.buffer), 28 + 4067 - 1)
        self.assertEqual(decoder.feed(partial[-1:] + frame_bytes()), [expected_image()])

    def test_active_image_is_bounded_by_total_and_global_limit(self):
        decoder = stream.ImageDecoder()
        total = stream.MAX_IMAGE_SIZE
        self.assertEqual(decoder.feed(packet(1, struct.pack("<HH", WIDTH, HEIGHT), total=total)), [])
        for offset in range(0, total, 4067):
            payload = b"x" * min(4067, total - offset)
            self.assertEqual(decoder.feed(packet(2, payload, offset=offset, total=total)), [])
            self.assertLessEqual(len(decoder.active[-1]), total)
            self.assertEqual(decoder.buffer, b"")
        self.assertEqual(len(decoder.active[-1]), total)
        self.assertEqual(decoder.feed(packet(2, b"x", offset=total, total=total)), [])
        self.assertIsNone(decoder.active)
        self.assertEqual(decoder.feed(frame_bytes(frame_id=2)), [expected_image(2)])

    def test_missing_duplicate_and_out_of_order_chunks(self):
        packets = frame_packets()
        cases = {
            "missing_first": packets[:1] + packets[2:],
            "missing_middle": packets[:2] + packets[3:],
            "missing_last": packets[:-2] + packets[-1:],
            "duplicate": packets[:2] + packets[1:],
            "out_of_order": packets[:1] + [packets[2], packets[1]] + packets[3:],
        }
        for name, bad in cases.items():
            with self.subTest(name=name):
                self.assert_recovers(b"".join(bad))

    def test_frame_id_or_total_changes_cannot_splice_images(self):
        begin = frame_packets()[0]
        for changes in ({"frame_id": 2}, {"total": len(JPEG) + 1}):
            with self.subTest(changes=changes):
                self.assert_recovers(begin + packet(2, JPEG[:2000], **changes))
        self.assert_recovers(begin + packet(3, struct.pack("<I", zlib.crc32(JPEG)),
                                           frame_id=2, offset=len(JPEG)))

    def test_bad_frame_crc_even_with_valid_end_packet_crc(self):
        packets = frame_packets()
        bad_end = packet(3, struct.pack("<I", zlib.crc32(JPEG) ^ 1), offset=len(JPEG))
        self.assert_recovers(b"".join(packets[:-1]) + bad_end)

    def test_abort_ignores_following_data_and_end(self):
        packets = frame_packets()
        abort = packet(4, struct.pack("<I", 1), offset=2000)
        decoder = self.assert_recovers(b"".join(packets[:2]) + abort + b"".join(packets[2:]))
        self.assertEqual(decoder.discarded_frames, 1)

    def test_dropped_abort_and_stale_prefix_are_superseded_by_begin(self):
        packets = frame_packets()
        for prefix in (packets[0], b"".join(packets[:2]), b"".join(packets[:-1])):
            with self.subTest(prefix_length=len(prefix)):
                decoder = self.assert_recovers(prefix)
                self.assertEqual(decoder.discarded_frames, 1)

    def test_no_completion_without_begin_all_data_and_full_end(self):
        packets = frame_packets()
        for prefix in (b"".join(packets[1:]), b"".join(packets[:-1]), frame_bytes()[:-1]):
            with self.subTest(length=len(prefix)):
                self.assertEqual(stream.ImageDecoder().feed(prefix), [])

    def test_real_jpeg_sof_dimensions_not_transport_metadata(self):
        for dimensions in ((1, 1), (HEIGHT, WIDTH), (0, HEIGHT), (WIDTH, 0)):
            with self.subTest(dimensions=dimensions):
                self.assert_recovers(frame_bytes(dimensions=dimensions),
                                     discarded=all(dimensions))

    def test_invalid_jpeg_with_valid_transport_checksums_is_rejected(self):
        bad_jpegs = [b"not a JPEG", JPEG[2:], JPEG[:-2], b"\xff\xd8\xff\xd9",
                     b"\xff\xd8\xff\xe0\x00\x01\xff\xd9",
                     b"\xff\xd8\xff\xe0\xff\xff\xff\xd9",
                     b"\xff\xd8\xff\xc0\x00\x07\x08\x00\x01\x00\x01\xff\xd9",
                     b"\xff\xd8\xff\xc0\x00\x08\x08\x00\x00\x00\x01\x01\xff\xd9"]
        for jpeg in bad_jpegs:
            with self.subTest(jpeg=jpeg[:16]):
                self.assert_recovers(frame_bytes(jpeg=jpeg))
                with self.assertRaises(ValueError):
                    stream.jpeg_dimensions(jpeg)

    def test_synthetic_sof_metadata_is_not_claimed_to_be_pixel_decodable(self):
        # This deliberately lacks Huffman tables and scan data. It tests marker
        # parsing only; accepting SOF metadata is NOT full JPEG validation.
        jpeg = (b"\xff\xd8\xff\xff\xe0\x00\x04ab\xff\x01\xff\xc2"
                + struct.pack(">HBHHB", 8, 8, HEIGHT, WIDTH, 1) + b"\xff\xd9")
        self.assertEqual(stream.jpeg_dimensions(jpeg), (WIDTH, HEIGHT))
        self.assertEqual(stream.ImageDecoder().feed(frame_bytes(jpeg=jpeg)),
                         [expected_image(jpeg=jpeg)])


class FakeJLink:
    def __init__(self, images=(), logs=()):
        self.channels = {2: iter(images), 1: iter(logs)}
        self.reads = []

    def rtt_read(self, channel, count):
        self.reads.append((channel, count))
        return list(next(self.channels[channel], b""))


class CaptureTests(unittest.TestCase):
    def capture(self, jlink, directory, iterations, logfile=None):
        # Initial call sets deadline, one call per loop condition, last expires.
        clock = [0] + list(range(iterations)) + [iterations]
        with mock.patch.object(rtt_logs.time, "monotonic", side_effect=clock), \
                mock.patch.object(rtt_logs.time, "sleep") as sleep, \
                contextlib.redirect_stdout(io.StringIO()) as stdout:
            decoder = rtt_logs.capture_channels(jlink, logfile, directory, duration=iterations)
        self.assertEqual(jlink.reads, [(channel, count) for _ in range(iterations)
                                     for channel, count in ((2, 4096), (1, 2048))])
        return decoder, stdout.getvalue(), sleep

    def test_channel_two_drained_without_text_logs(self):
        wire = frame_bytes()
        chunks = [wire[offset:offset + 173] for offset in range(0, len(wire), 173)]
        jlink = FakeJLink(chunks)
        with tempfile.TemporaryDirectory() as directory:
            decoder, stdout, sleep = self.capture(jlink, directory, len(chunks))
            paths = list(Path(directory).glob("*.jpg"))
            self.assertEqual(len(paths), 1)
            self.assertEqual(paths[0].read_bytes(), JPEG)
            self.assertIn("frame-1_1600x1200.jpg", paths[0].name)
            self.assertIn("[camera] Saved", stdout)
            self.assertIsNone(decoder.active)
            sleep.assert_not_called()

    def test_channel_one_still_captured_and_flushed_while_images_drain(self):
        jlink = FakeJLink([frame_bytes(), b""], [b"log one\n", b"bad byte: \xff\n"])
        logfile = mock.Mock(wraps=io.StringIO())
        with tempfile.TemporaryDirectory() as directory:
            _, stdout, sleep = self.capture(jlink, directory, 2, logfile)
            self.assertEqual(len(list(Path(directory).iterdir())), 1)
        self.assertEqual(logfile.getvalue(), "log one\nbad byte: \ufffd\n")
        self.assertEqual(logfile.flush.call_count, 2)
        self.assertIn("log one\n", stdout)
        self.assertIn("bad byte: \ufffd\n", stdout)
        sleep.assert_not_called()

    def test_idle_capture_sleeps_and_duration_expires(self):
        with tempfile.TemporaryDirectory() as directory:
            _, stdout, sleep = self.capture(FakeJLink(), directory, 3)
            self.assertEqual(list(Path(directory).iterdir()), [])
        self.assertEqual(stdout, "")
        self.assertEqual(sleep.call_args_list, [mock.call(0.005)] * 3)

    def test_only_valid_completed_images_create_files(self):
        packets = frame_packets()
        cases = {
            "missing_end": b"".join(packets[:-1]),
            "truncated_end": frame_bytes()[:-1],
            "missing_chunk": b"".join(packets[:2] + packets[3:]),
            "aborted": b"".join(packets[:2]) + packet(4, struct.pack("<I", 1), offset=2000),
            "frame_crc": b"".join(packets[:-1]) + packet(3, b"\x00" * 4, offset=len(JPEG)),
            "dimensions": frame_bytes(dimensions=(1, 1)),
            "not_jpeg": frame_bytes(jpeg=b"not jpeg"),
        }
        for name, wire in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                destination = Path(directory) / "images"
                self.capture(FakeJLink([wire]), destination, 1)
                self.assertFalse(destination.exists(), "partial/invalid frames must not create files")
                self.capture(FakeJLink([wire + frame_bytes(frame_id=2)]), destination, 1)
                paths = list(destination.iterdir())
                self.assertEqual(len(paths), 1)
                self.assertIn("frame-2_1600x1200.jpg", paths[0].name)
                self.assertEqual(paths[0].read_bytes(), JPEG)

    def test_save_image_does_not_overwrite_existing_capture(self):
        fixed_datetime = mock.Mock()
        fixed_datetime.datetime.now.return_value.strftime.return_value = "fixed"
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(rtt_logs, "datetime", fixed_datetime):
            path = rtt_logs.save_image(expected_image(), Path(directory) / "nested")
            self.assertEqual(path.read_bytes(), JPEG)
            with self.assertRaises(FileExistsError):
                rtt_logs.save_image(expected_image(jpeg=b"replacement"), path.parent)
            self.assertEqual(path.read_bytes(), JPEG)

    def test_imports_do_not_require_pylink(self):
        result = subprocess.run(
            [sys.executable, "-B", "-c",
             "import sys; sys.modules['pylink'] = None; import camera_stream, rtt_logs"],
            cwd=Path(__file__).resolve().parent, capture_output=True, text=True, timeout=5,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_open_rtt_channels_cleanup_success_and_failures(self):
        for failure in (None, "connect", "rtt_start", "capture", "rtt_stop"):
            with self.subTest(failure=failure):
                jlink = mock.Mock()
                events = mock.Mock()
                events.attach_mock(jlink, "jlink")
                capture = mock.Mock(return_value="decoder")
                events.attach_mock(capture, "capture")
                if failure == "capture":
                    capture.side_effect = RuntimeError("capture failed")
                elif failure:
                    getattr(jlink, failure).side_effect = RuntimeError(f"{failure} failed")
                module = types.ModuleType("pylink")
                module.JLink = mock.Mock(return_value=jlink)
                with mock.patch.dict(sys.modules, {"pylink": module}), \
                        mock.patch.object(rtt_logs, "capture_channels", capture):
                    if failure:
                        with self.assertRaisesRegex(RuntimeError, f"{failure} failed"):
                            rtt_logs.open_rtt_channels("log", "images", 2)
                    else:
                        self.assertEqual(rtt_logs.open_rtt_channels("log", "images", 2), "decoder")
                calls = [mock.call.jlink.open(), mock.call.jlink.connect(chip_name="ATSAMD51P20A")]
                if failure != "connect":
                    calls.append(mock.call.jlink.rtt_start())
                    if failure != "rtt_start":
                        calls += [mock.call.capture(jlink, "log", "images", 2), mock.call.jlink.rtt_stop()]
                calls.append(mock.call.jlink.close())
                self.assertEqual(events.mock_calls, calls)
                module.JLink.assert_called_once_with()


def jpeg_driver_harness():
    """Adapt the shared harness in memory, never its file or production driver."""
    harness = driver_tests.HARNESS
    start = harness.index("static uint8_t image_byte(size_t offset) {")
    end = harness.index("\n}\n", start) + len("\n}\n")
    fixture = ",".join(str(byte) for byte in JPEG)
    replacement = (f"static const uint8_t jpeg_fixture[] = {{{fixture}}};\n"
                   "static uint8_t image_byte(size_t offset) {\n"
                   "    CHECK(offset < sizeof(jpeg_fixture));\n"
                   "    return jpeg_fixture[offset];\n}\n")
    harness = harness[:start] + replacement + harness[end:]
    old = "static size_t image_size = 9007, fifo_offset, output_size;"
    if harness.count(old) != 1:
        raise AssertionError("shared harness image_size declaration changed")
    harness = harness.replace(old, f"static size_t image_size = {len(JPEG)}, fifo_offset, output_size;")
    # Current harness already drains the actual bounded ring and emits WIRE hex.
    # Fail loudly if that contract changes instead of synthesizing Python packets.
    if 'printf("WIRE ");' not in harness:
        raise AssertionError("shared harness no longer exports captured ring bytes")
    return harness


class FirmwareInteroperabilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Reuse its compiler flags, actual driver/register sources, stub headers,
        # temporary-directory cleanup and build diagnostics without inheriting its
        # unrelated test methods or duplicating hundreds of lines of C mocks.
        with mock.patch.object(driver_tests, "HARNESS", jpeg_driver_harness()):
            driver_tests.ArducamDriverTests.setUpClass.__func__(cls)

    def wire(self, case, *args):
        result = subprocess.run([str(self.binary), case, *args], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        wire_line, pass_line = result.stdout.splitlines()
        self.assertEqual(pass_line, f"PASS {case}")
        self.assertTrue(wire_line.startswith("WIRE "))
        return bytes.fromhex(wire_line[5:])

    def test_actual_c_driver_stream_decodes_with_real_multichunk_jpeg(self):
        for case in ("success", "retry"):
            wire = self.wire(case)
            for read_size in (1, 27, 4096, len(wire)):
                with self.subTest(case=case, read_size=read_size):
                    decoder = stream.ImageDecoder()
                    images = []
                    for offset in range(0, len(wire), read_size):
                        images.extend(decoder.feed(wire[offset:offset + read_size]))
                    self.assertEqual(images, [expected_image()])
                    self.assertEqual((decoder.bad_packets, decoder.discarded_frames), (0, 0))

    def test_actual_c_stale_ring_prefix_with_delivered_or_dropped_abort(self):
        for args in ((), ("dropped",)):
            with self.subTest(args=args):
                decoder = stream.ImageDecoder()
                self.assertEqual(decoder.feed(self.wire("stale_retry", *args)), [expected_image(2)])
                self.assertEqual(decoder.discarded_frames, 1)
                self.assertEqual(decoder.bad_packets, 0)

    def test_actual_c_cancel_and_spi_failure_never_complete(self):
        for case, args in (("cancel", ("3",)), ("spi_failure", ("transfer", "2"))):
            with self.subTest(case=case):
                decoder = stream.ImageDecoder()
                self.assertEqual(decoder.feed(self.wire(case, *args)), [])
                self.assertEqual(decoder.feed(frame_bytes(frame_id=99)), [expected_image(99)])


if __name__ == "__main__":
    unittest.main()
