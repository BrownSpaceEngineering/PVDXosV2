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

The first request initializes the RTT image channel after the scheduler starts.
Every request initializes the camera hardware, captures, then puts the camera
back to sleep with `arducam_sleep()`, whether or not the capture worked.
Initialization and capture failures are returned to the command executor.

## Low power

The camera has no switchable supply, so it is put to sleep over SPI, through the
ArduChip, in two ways at once:

1. The sensor's PWDN pin: bit 1 of `0x05` (direction) and `0x06` (value). Under
   15 µA for the sensor, per the OV2640 datasheet.
2. The ArduChip's low-power mode: `0x03` bit 6, which also powers down the FIFO
   memory. The ArduCAM datasheet gives 20 mA at 5 V for the whole module, against
   70 mA running.

Not the OV2640's software standby (`COM2` bit 4): on our board the sensor holds
SDA low once it enters it, which takes down the whole shared I2C bus (fuel
gauges, PMB) until the sensor is reset or power-cycled.

`init_arducam_hardware()` wakes the camera if it was asleep (waiting
`ARDUCAM_WAKE_DELAY_MS`, as the datasheet gives no wake time), reinitializes the
sensor, then waits `ARDUCAM_EXPOSURE_SETTLE_MS` for auto-exposure, without which
captures come out black. The startup check and the burn-sequence picture also
put the camera to sleep when they finish, so it sleeps from boot onward, and
because every capture ends asleep, disabling the task leaves it asleep. On
hardware, sleeping and waking leaves the I2C bus working and captures normal
800x600 JPEGs; the current savings have not been measured.

The ArduChip and sensor keep their registers across MCU resets. If an
experiment leaves them in a bad state (8-byte captures of zeros), only a power
cycle, or pulsing the sensor power enable (`0x06` bit 2) and resetting the
ArduChip (`0x07` = 0x80, then 0x00), brings them back.

Captures are 800x600 JPEGs (`ARDUCAM_DEFAULT_RESOLUTION`). `init_arducam_hardware()`
takes any `arducam_resolution_t`, from 160x120 up to 1600x1200 (UXGA, the
OV2640's maximum); the size holds until the next init. Captures use the
existing 4 KB transfer buffer rather than a full frame in MCU RAM. Frames at or above the driver's FIFO limit (`OV2640_MAX_FIFO_SIZE`,
currently `0x5FFFF`) are rejected. JPEG size depends on the scene/compression.
No image is stored persistently on the satellite, no camera telemetry is
published, and no photo-ready-for-downlink input is set. This RTT path remains
a development transport, not flight image storage/downlink.

## Restart markers

The OV2640 doesn't put restart markers in its JPEGs (at least our unit,
revision 0x42, doesn't, and no register setting turns them on), so a single
lost downlink packet would corrupt the rest of the image. `capture_rtt()` and
`capture_mram()` therefore pass every frame through `jpeg_add_restart_markers()`
(`src/drivers/arducam/jpeg_restart.c`), which adds a restart marker after every
MCU row (8 pixel rows for the OV2640's 4:2:2 JPEGs) as it streams the frame out
of the FIFO. Only the DC coefficients are re-encoded, so the decoded image is
unchanged; images grow by 1–3%. If a frame can't be converted, the capture
fails, like any other capture error.

On the ground, `scripts/jpeg_repair.py` rebuilds a decodable JPEG from an image
with byte ranges missing: bands that arrived whole decode in place, and lost
ones come out grey. On the bench, losing one 492-byte CFDP segment of an 800x600
image greyed out 24 of 600 rows; without restart markers, everything after the
loss was garbage. The header (the first ~620 bytes) has to arrive.

## Receive images

`capture_rtt()` streams the JPEG on RTT channel 2, between `IMG_BEGIN` and
`IMG_END len=<bytes>` lines on log channel 1. The length comes at the end, since
it isn't known until the frame has been through the restart-marker conversion,
and the ArduChip can't rewind its FIFO to read a frame twice.
`scripts/rtt_logs.py` saves each image to `logs/<timestamp>_capture.jpg`. Don't
run another channel-2 reader at the same time: RTT reads consume bytes.

The camera's SPI runs at 1 MHz (`ARDUCAM_SPI_HZ`). The transfer and conversion
take about 1.2 s for 800x600 and 4.3 s for 1600x1200.

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

`SPI_CAMERA` is camera-owned. `I2C_SENSORS` is shared with fuel gauges and PMB;
all three drivers use `i2c_sensors_bus_mutex`. The mutex is created before startup
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
