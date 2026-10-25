"""Incremental decoder for the firmware's PVIM v1 packets on RTT up-channel 2.

No hardware or third-party modules are needed to decode an already captured stream.
Only complete, checksummed JPEGs whose SOF dimensions match BEGIN are returned.
"""

from dataclasses import dataclass
import struct
import zlib

MAGIC = b"PVIM"
HEADER = struct.Struct("<4sBBHIIIII")
BEGIN, DATA, END, ABORT = 1, 2, 3, 4
MAX_PAYLOAD = 4067
MAX_IMAGE_SIZE = 0x5FFFF - 1


@dataclass(frozen=True)
class Image:
    frame_id: int
    width: int
    height: int
    jpeg: bytes


def jpeg_dimensions(jpeg):
    """Read JPEG SOF dimensions without decoding pixels or trusting transport metadata."""
    if not jpeg.startswith(b"\xff\xd8") or not jpeg.endswith(b"\xff\xd9"):
        raise ValueError("missing JPEG SOI/EOI")
    offset = 2
    while offset < len(jpeg):
        if jpeg[offset] != 0xFF:
            raise ValueError("invalid JPEG marker")
        while offset < len(jpeg) and jpeg[offset] == 0xFF:
            offset += 1
        if offset >= len(jpeg):
            break
        marker = jpeg[offset]
        offset += 1
        if marker in (0xDA, 0xD9):
            break
        if marker == 0x01 or 0xD0 <= marker <= 0xD8:
            continue
        if offset + 2 > len(jpeg):
            break
        size = int.from_bytes(jpeg[offset:offset + 2], "big")
        if size < 2 or offset + size > len(jpeg):
            raise ValueError("invalid JPEG segment length")
        if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7,
                      0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            if size < 8:
                raise ValueError("short JPEG SOF")
            height, width = struct.unpack_from(">HH", jpeg, offset + 3)
            if not width or not height:
                raise ValueError("empty JPEG dimensions")
            return width, height
        offset += size
    raise ValueError("JPEG has no supported SOF marker")


class ImageDecoder:
    """Resynchronize using header CRCs; never save a partial, aborted, or corrupt frame."""

    def __init__(self):
        self.buffer = bytearray()
        self.active = None
        self.bad_packets = 0
        self.discarded_frames = 0

    def _discard(self):
        if self.active is not None:
            self.discarded_frames += 1
        self.active = None

    def feed(self, data):
        self.buffer.extend(data)
        images = []
        while True:
            start = self.buffer.find(MAGIC)
            if start < 0:
                # A magic prefix may be split between successive RTT reads.
                if len(self.buffer) > len(MAGIC) - 1:
                    del self.buffer[:-(len(MAGIC) - 1)]
                break
            if start:
                del self.buffer[:start]
            if len(self.buffer) < HEADER.size:
                break
            header = bytes(self.buffer[:HEADER.size])
            _, version, kind, size, frame_id, offset, total, payload_crc, header_crc = HEADER.unpack(header)
            valid_shape = (0 < total <= MAX_IMAGE_SIZE and
                           ((kind == BEGIN and size == 4 and offset == 0) or
                            (kind == DATA and 0 < size <= MAX_PAYLOAD and offset + size <= total) or
                            (kind == END and size == 4 and offset == total) or
                            (kind == ABORT and size == 4 and offset <= total)))
            if version != 1 or not valid_shape or zlib.crc32(header[:24]) != header_crc:
                self.bad_packets += 1
                self._discard()
                del self.buffer[0]
                continue
            packet_size = HEADER.size + size
            if len(self.buffer) < packet_size:
                break
            payload = bytes(self.buffer[HEADER.size:packet_size])
            if zlib.crc32(payload) != payload_crc:
                self.bad_packets += 1
                self._discard()
                # Search again rather than trusting the boundary of a corrupt packet.
                del self.buffer[0]
                continue
            del self.buffer[:packet_size]
            if kind == BEGIN:
                self._discard()
                width, height = struct.unpack("<HH", payload)
                if width and height:
                    self.active = (frame_id, total, width, height, bytearray())
                continue
            if self.active is None:
                continue
            active_id, active_total, width, height, jpeg = self.active
            if (frame_id, total) != (active_id, active_total):
                # Old packets cannot be spliced into a newer frame.
                self._discard()
                continue
            if kind == ABORT:
                self._discard()
            elif kind == DATA:
                if offset != len(jpeg):
                    self._discard()
                else:
                    jpeg.extend(payload)
            elif kind == END:
                complete = len(jpeg) == total and zlib.crc32(jpeg) == struct.unpack("<I", payload)[0]
                if complete:
                    try:
                        complete = jpeg_dimensions(jpeg) == (width, height)
                    except ValueError:
                        complete = False
                if complete:
                    images.append(Image(frame_id, width, height, bytes(jpeg)))
                    self.active = None
                else:
                    self._discard()
        return images
