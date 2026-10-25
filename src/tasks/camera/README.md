# Camera task

The camera task exclusively owns the Arducam OV2640 driver in
`src/drivers/arducam/`. Its stack, TCB, command queue, lifecycle acknowledgement,
and RTT image buffer are statically allocated. It replaces the standalone
periodic Arducam task from `simon-drivers-on-main`.

The task manager creates it at startup but leaves it suspended. No mission state
currently enables it; add `p_camera_task` to the appropriate state's
`enabled_tasks` when needed. Enabling it does not automatically take pictures.

## Capture commands

Include `camera_task.h` and `cmd_dispatcher.h`, then submit a request from a
running task:

```c
command_t cmd = get_camera_capture_command();
status_t queued = enqueue_command(&cmd);
```

`queued` reports queue admission, not capture completion. Commands are copied
into the queue, so the caller's `cmd.result` is not updated. A successful framed
END packet indicates stream completion; a general asynchronous command-result
API is not implemented.

The first request initializes the RTT image channel and camera hardware after
the scheduler starts. Initialization and capture failures are returned to the
command executor; subsequent requests retry initialization as needed.

Captures are 1600x1200 (UXGA, the OV2640's maximum supported output resolution)
JPEGs. They use the existing 4 KB transfer buffer rather than a full frame in
MCU RAM. Frames at or above the driver's FIFO limit (`OV2640_MAX_FIFO_SIZE`,
currently `0x5FFFF`) are rejected. JPEG size depends on the scene/compression.
No image is stored persistently on the satellite, no camera telemetry is
published, and no photo-ready-for-downlink input is set. This RTT path remains
a development transport, not flight image storage/downlink.

## Receive images

The standard `scripts/rtt_logs.py` reader now drains log channel 1 **and** framed
image channel 2 through the same J-Link connection. It saves only complete,
checksummed JPEGs with SOF dimensions matching BEGIN to `logs/images/`; logs
remain in `logs/`. It does not require `info()` messages or a development build
to assemble images. Do not run another channel-2 reader concurrently: RTT reads
consume bytes.

For a bounded capture session, from the repository root:

```sh
python3 scripts/rtt_logs.py --duration 30
```

Optional output path: `--images-dir logs/my-captures`. The existing
`tools/rtt.sh start|stop|status|tail` background workflow also receives images,
because it launches this reader. Python requires `pylink-square` and the SEGGER
J-Link library; offline decoding and host tests need neither.

The reader does not enable the camera task or issue capture commands. Enable
and request captures through firmware/task-manager control separately.

## PVIM v1 wire format

This replaces the previous raw JPEG channel and log-based `IMG_BEGIN`/`IMG_END`
framing. Custom receivers must be updated to use PVIM packets; the included
`camera_stream.ImageDecoder` can decode arbitrary-sized chunks offline.

Every channel-2 packet has a 28-byte, explicitly little-endian header, equivalent
to Python `struct.Struct("<4sBBHIIIII")`:

| Offset | Field                                      |
| ------ | ------------------------------------------ |
| 0      | Four-byte magic `PVIM`                     |
| 4      | Version, `1` (u8)                          |
| 5      | Type (u8): BEGIN=1, DATA=2, END=3, ABORT=4 |
| 6      | Payload byte count (u16, maximum 4067)     |
| 8      | Frame attempt ID (u32)                     |
| 12     | JPEG byte offset (u32)                     |
| 16     | Total JPEG byte count (u32)                |
| 20     | CRC32 of payload (u32)                     |
| 24     | CRC32 of the first 24 header bytes (u32)   |

CRCs use IEEE CRC32, compatible with `zlib.crc32`. BEGIN carries width and height
as two u16 values and offset zero. DATA carries contiguous JPEG bytes. END has
offset equal to the total and carries the whole JPEG's CRC32 as one u32. ABORT
carries a `status_t` value as a u32 and the accepted JPEG-byte offset. Frame IDs
increment for each attempt; they can wrap or restart after a board reset.

Packets are written atomically using `NO_BLOCK_SKIP` in every build. The driver
retries a complete packet without rereading FIFO bytes, but returns
`ERROR_WRITE_FAILED` if the host cannot accept it within 1 second
(`ARDUCAM_RTT_TIMEOUT_MS`). This is a per-packet no-progress timeout, not a
whole-image deadline: UXGA transfers can take several seconds at 1 MHz SPI.

ABORT is best effort and may not fit if the host has stopped reading. A valid
new BEGIN replaces any incomplete frame, including stale bytes still queued
from an earlier attempt. The receiver validates packet CRCs, frame IDs, lengths,
contiguous offsets, and the final JPEG CRC before saving anything. It also
checks JPEG SOI/EOI and SOF dimensions, but does not decode/validate every pixel.
The target never rewinds the host's RTT read pointer to discard stale bytes.

## Safe disable and shared hardware

The camera uses optional `pvdx_task_t` lifecycle hooks. On disable, the task
manager closes command admission, requests cooperative stop, and waits without
holding the task-list mutex. The driver checks cancellation while polling
capture completion, between transfers, and while waiting for RTT space. It
releases SPI chip select and returns before the camera acknowledges the safe
point. The camera then parks without hardware access; only after acknowledgement
does the manager suspend it and unregister it from the watchdog.

Queued capture commands are discarded during shutdown and again before resume.
The dispatcher makes its enabled check and zero-wait enqueue atomic, so a
producer cannot pause across a disable/resume cycle and insert an old request.
The resume hook clears the stop flag before the task runs again. Other tasks
without lifecycle hooks retain their existing disable behavior.

The stop acknowledgement has a 3-second deadline. Camera initialization must
finish before acknowledgement, although it does not itself support cancellation.
The synchronous ASF HAL can still stall inside a hardware transfer; in that
case timeout/watchdog recovery is necessary. The manager reports a fatal safe
stop timeout rather than suspending a task that may own hardware resources.

`SPI_CAMERA` is camera-owned. `I2C_CAMERA` is shared with fuel gauges and PMB;
all three drivers use `i2c_camera_bus_mutex`. The mutex is created before startup
checks/tests. Camera initialization preserves generated 100 kHz I2C timing and
sets 1 MHz SPI using the ASF BAUD-divider API.

## Host regression tests

```sh
python3 scripts/test_camera_task.py -v
python3 scripts/test_camera_lifecycle.py -v
python3 scripts/test_arducam_driver.py -v
python3 scripts/test_camera_stream.py -v
```

Tests cover command handling, safe disable/resume, atomic admission, actual
C-driver packets through a bounded RTT ring, cancellation, stale-prefix recovery,
SPI/I2C failures (including final UXGA-table writes), reset-delay ordering, host
fragmentation/corruption, and C-to-Python protocol interoperability with a JPEG
fixture. Real-board image dimensions, electrical operation, and image quality
still require hardware verification.
