# Scripts

The scripts folder is comprised of a collection of scripts that are used to automate various tasks or interact with PVDXos in some way. They are _never_ linked together with the main application (i.e. are not included in PVDXos itself).

## RTT logs and camera images

`rtt_logs.py` drains diagnostic channel 1 and PVIM-framed image channel 2 through
one J-Link connection. It writes timestamped logs under `logs/` and validated,
complete JPEGs under `logs/images/`. It does not trigger captures or enable tasks.
Install `pylink-square` and the SEGGER J-Link library for live reading.

```sh
python3 scripts/rtt_logs.py --duration 30
# Or use the existing background wrapper:
tools/rtt.sh start
tools/rtt.sh stop
```

Use `--images-dir PATH` to change image output. No duration means run until
interrupted; agents should use `--duration` or the background wrapper. Do not run
multiple readers on image channel 2. The host receiver requires the PVIM firmware
protocol; older raw-JPEG streams are not supported.

`camera_stream.py` is the hardware-independent decoder. See
[`src/tasks/camera/README.md`](../src/tasks/camera/README.md) for protocol, capture
commands, lifecycle behavior, and the four host regression-test commands.
