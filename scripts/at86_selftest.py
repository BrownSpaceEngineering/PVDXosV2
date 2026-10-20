#!/usr/bin/env python3
"""
Self-test for a SAMD51 + AT86RF215 board that runs the link test firmware (`make test TESTS="at86_link"`).

It checks that the parts talk to each other and that the test loop keeps running:
  - Startup: the SAMD51 reads a valid AT86RF215 part number over SPI, the front end is in PADFE configuration 1 with
    AUXS.EXTLNABYP = 0 (FEA09 = GRF5504 enable only in TX), and the PLL locks.
  - Live: the firmware prints an "at86hb" heartbeat every 2 s. Each heartbeat re-reads the part number and the front
    end registers over SPI. The script checks that heartbeats keep coming, that the poll counter and uptime grow,
    that SPI and front end reads stay good, that the radio state is valid, that the radio keeps cycling between TX
    and RX, and that the board does not reset.
  - Optional (--gdb): two snapshots of the global at86_link_status over GDB, to confirm the RTT view. This halts
    the core for about a second each time.
  - Optional (--expect-traffic): the HackRF is running scripts/hackrf_link.py, so ACKs and DATA must arrive.

The script only reads. It uses the repository wrappers: tools/rtt.sh for the log and tools/gdb.sh for GDB.

Usage:
  python3 scripts/at86_selftest.py                     # 60 s live check (starts tools/rtt.sh if needed)
  python3 scripts/at86_selftest.py --duration 300 --expect-traffic
  python3 scripts/at86_selftest.py --gdb               # also cross-check with GDB (needs tools/jlink.sh start)
  python3 scripts/at86_selftest.py --file logs/2026-10-20_12-00-00.log   # check a saved log offline

Exit code: 0 if no check failed, 1 otherwise.
"""

import argparse
import os
import re
import subprocess
import sys
import time

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DEFAULT_LOG = "/tmp/pvdxos-rtt.log"  # tools/rtt.sh output
HEARTBEAT_S = 2.0  # LINK_HEARTBEAT_MS
STATUS_MAGIC = 0xA7860215  # AT86_LINK_STATUS_MAGIC
VALID_PN = {0x34: "AT86RF215", 0x35: "AT86RF215IQ", 0x36: "AT86RF215M"}
RF_STATES = {2: "TRXOFF", 3: "TXPREP", 4: "TX", 5: "RX", 6: "TRANSITION", 7: "RESET"}
OK_STATES = {3, 4, 5, 6}
PHASES = {0: "INIT", 1: "A_TX_DATA", 2: "A_WAIT_ACK", 3: "B_WAIT_DATA", 4: "B_TX_ACK", 5: "B_DUP_WINDOW", 6: "DONE", 7: "INIT_FAILED"}
MAX_IDLE_RADIO_S = 40  # Longest time with no TX and no new RX listen (phase B waits up to 30 s)

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
KV = re.compile(r"(\w+)=(\S+)")


def parse_kv(text):
    out = {}
    for k, v in KV.findall(text):
        try:
            out[k] = int(v, 0)
        except ValueError:
            out[k] = v
    return out


class Results:
    def __init__(self):
        self.rows = []

    def add(self, status, name, detail=""):
        self.rows.append((status, name, detail))
        print(f"{status:<5} {name}{('  ' + detail) if detail else ''}", flush=True)

    def ok(self, cond, name, detail="", fail="FAIL"):
        self.add("PASS" if cond else fail, name, detail)
        return cond

    @property
    def failed(self):
        return sum(1 for r in self.rows if r[0] == "FAIL")


class LogState:
    """Collects the lines of interest from the firmware log."""

    def __init__(self):
        self.boot = {}
        self.hb = {}  # n -> merged dict, with "_wall" = host time of arrival
        self.hb_order = []
        self.resets = []
        self.asserts = []
        self.fatals = []
        self.rounds = []
        self.packets = 0
        self.saw_start = False
        self.epoch = 0  # Counts "at86link start" lines, so heartbeat numbers that restart after a reset do not mix

    def line(self, raw, wall):
        s = ANSI.sub("", raw).strip()
        if not s:
            return
        if "Bootloader executed" in s or "at86link start" in s:
            if self.saw_start or self.hb:
                self.resets.append((wall, s))
            if "at86link start" in s:
                self.saw_start = True
                self.epoch += 1
                self.boot = {"start": parse_kv(s.split("at86link start", 1)[1])}
        m = re.search(r"at86link (init|fe|phy|pll|ready|done)(.*)", s)
        if m:
            key = m.group(1)
            if "FAIL" in s:
                self.boot["fail"] = s
            self.boot[key] = parse_kv(m.group(2))
            self.boot[key]["_line"] = s
        if "ASSERT FAILED" in s:
            self.asserts.append(s)
        if "[FATAL" in s:
            self.fatals.append(s)
        m = re.search(r"at86hb(2?) (n=\d+.*)", s)
        if m:
            kv = parse_kv(m.group(2))
            n = kv.get("n")
            if isinstance(n, int):
                key = (self.epoch, n)
                if key not in self.hb:
                    self.hb[key] = {"_wall": wall}
                    self.hb_order.append(key)
                self.hb[key].update(kv)
        m = re.search(r"at86round (n=\d+.*)", s)
        if m:
            self.rounds.append(parse_kv(m.group(1)))
        if "at86pkt" in s:
            self.packets += 1

    def complete_heartbeats(self):
        need = ("t", "polls", "pn", "st", "spierr", "padfe", "auxs", "feerr", "tx", "rxl", "txerr")
        # Only the latest boot: after a reset the counters start again
        return [self.hb[k] for k in self.hb_order if k[0] == self.epoch and all(f in self.hb[k] for f in need)]


def sh(cmd, timeout=60, env=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    p = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, timeout=timeout, env=e)
    return p.returncode, p.stdout + p.stderr


def ensure_rtt(res, args):
    rc, out = sh(["tools/rtt.sh", "status"])
    if "CAPTURING" in out and "NOT" not in out:
        res.add("PASS", "RTT capture running", out.strip())
        return True
    if args.no_start:
        res.add("FAIL", "RTT capture running", "not running and --no-start given (run: tools/rtt.sh start)")
        return False
    rc, out = sh(["tools/rtt.sh", "start"], timeout=30)
    return res.ok(rc == 0, "RTT capture started", out.strip().splitlines()[-1] if out.strip() else "")


def check_boot(res, st):
    if not st.saw_start:
        res.add(
            "SKIP",
            "startup checks",
            "no 'at86link start' line in the log. The capture began after boot; reset the board while tools/rtt.sh "
            "runs to see them (make reset_mcu, which needs approval on shared hardware)",
        )
        return
    b = st.boot
    init = b.get("init", {})
    pn = init.get("pn")
    res.ok(
        isinstance(pn, int) and pn in VALID_PN and init.get("conn") == 0,
        "SPI: AT86RF215 part number at startup",
        f"pn=0x{pn:02x} ({VALID_PN.get(pn, 'unknown')}) vn=0x{init.get('vn', 0):02x} conn={init.get('conn')}"
        if isinstance(pn, int)
        else "no init line",
    )
    fe = b.get("fe", {})
    if fe:
        padfe, auxs = fe.get("padfe", 0), fe.get("auxs", 0)
        res.ok(
            (padfe >> 6) == 1 and not (auxs & 0x80),
            "front end: PADFE config 1, EXTLNABYP = 0",
            f"padfe=0x{padfe:02x} auxs=0x{auxs:02x}",
        )
    else:
        res.add("FAIL", "front end: PADFE config 1, EXTLNABYP = 0", "no 'at86link fe' line")
    phy = b.get("phy", {})
    if phy:
        sfd = phy.get("sfd0")
        res.ok(sfd == 0x7209, "PHY: SFD0 register", f"sfd0=0x{sfd:04x} freq={phy.get('freq')} (pass --sfd to hackrf_link.py if not 0x7209)" if isinstance(sfd, int) else str(sfd), fail="WARN")
    pll = b.get("pll", {})
    res.ok(pll.get("locked") == 1, "PLL locked at the channel frequency", f"locked={pll.get('locked')}")
    res.ok("ready" in b and "fail" not in b, "link setup finished", b.get("fail", ""))


def check_live(res, st, args, window_s, live):
    hbs = st.complete_heartbeats()
    expected = window_s / HEARTBEAT_S
    if not res.ok(len(hbs) >= max(3, expected * 0.5), "heartbeats arrive", f"{len(hbs)} in {window_s:.0f} s (expected about {expected:.0f})"):
        if not hbs:
            res.add("FAIL", "test loop running", "no heartbeat at all: the test is not running, hung, or the log is not the RTT log")
            return

    # Gaps: wall clock when live, firmware uptime when reading a file
    key = "_wall" if live else "t"
    scale = 1.0 if live else 1e-3
    gaps = [(b[key] - a[key]) * scale for a, b in zip(hbs, hbs[1:])]
    if live and hbs:
        gaps.append(time.time() - hbs[-1]["_wall"])
    max_gap = max(gaps) if gaps else 0
    res.ok(max_gap <= 3 * HEARTBEAT_S, "no heartbeat gap longer than 6 s", f"longest gap {max_gap:.1f} s")

    ns = [h["n"] for h in hbs]
    missing = sum(b - a - 1 for a, b in zip(ns, ns[1:]) if b > a + 1)
    res.ok(missing == 0, "heartbeat numbers are consecutive", f"{missing} missing (dropped RTT lines)" if missing else "", fail="WARN")

    polls = [h["polls"] for h in hbs]
    res.ok(all(b > a for a, b in zip(polls, polls[1:])), "poll counter grows every heartbeat (loop not stuck)", f"{polls[0]} -> {polls[-1]}")

    t = [h["t"] for h in hbs]
    t_ok = all(b > a for a, b in zip(t, t[1:]))
    detail = f"{t[0]} -> {t[-1]} ms"
    if live and len(hbs) >= 3:
        wall = hbs[-1]["_wall"] - hbs[0]["_wall"]
        fw = (t[-1] - t[0]) / 1000
        if wall > 0:
            detail += f", firmware/host time ratio {fw / wall:.3f}"
            t_ok = t_ok and 0.85 < fw / wall < 1.15
    res.ok(t_ok, "firmware uptime grows at the real rate", detail)

    pns = {h["pn"] for h in hbs}
    spierr = [h["spierr"] for h in hbs]
    res.ok(
        len(pns) == 1 and next(iter(pns)) in VALID_PN and spierr[-1] == spierr[0],
        "SPI: part number re-read every heartbeat",
        f"pn={','.join(f'0x{p:02x}' for p in sorted(pns))} spi errors {spierr[0]} -> {spierr[-1]} in {hbs[-1].get('spichk', '?')} checks",
    )

    bad_fe = [h for h in hbs if (h["padfe"] >> 6) != 1 or (h["auxs"] & 0x80)]
    res.ok(
        not bad_fe and hbs[-1]["feerr"] == hbs[0]["feerr"],
        "front end registers stay correct",
        f"padfe=0x{hbs[-1]['padfe']:02x} auxs=0x{hbs[-1]['auxs']:02x} feerr {hbs[0]['feerr']} -> {hbs[-1]['feerr']}",
    )

    states = [h["st"] for h in hbs]
    seen = sorted({RF_STATES.get(s, str(s)) for s in states})
    res.ok(all(s in OK_STATES for s in states), "RF09 state is TXPREP/TX/RX", f"seen: {', '.join(seen)}")

    phases = [h.get("ph") for h in hbs]
    if 7 in phases:
        res.add("FAIL", "test phase", "INIT_FAILED")
    elif phases[-1] == 6:
        res.add("WARN", "test phase", "DONE: LINK_ROUNDS reached, the radio loop stopped")
    else:
        res.add("PASS", "test phase", f"now {PHASES.get(phases[-1], phases[-1])}, round {hbs[-1].get('rnd')}")

    activity = [h["tx"] + h["rxl"] for h in hbs]
    longest_idle, idle_start = 0.0, hbs[0]
    for a, b in zip(hbs, hbs[1:]):
        if b["tx"] + b["rxl"] > a["tx"] + a["rxl"]:
            idle_start = b
        longest_idle = max(longest_idle, (b[key] - idle_start[key]) * scale)
    if window_s < MAX_IDLE_RADIO_S and activity[-1] == activity[0]:
        res.add("WARN", "radio keeps cycling TX/RX", f"no new TX or RX in {window_s:.0f} s; run with --duration 60 or more")
    else:
        res.ok(
            longest_idle <= MAX_IDLE_RADIO_S and activity[-1] > activity[0],
            "radio keeps cycling TX/RX",
            f"tx {hbs[0]['tx']} -> {hbs[-1]['tx']}, rx listens {hbs[0]['rxl']} -> {hbs[-1]['rxl']}, longest idle {longest_idle:.0f} s",
        )
    res.ok(hbs[-1]["txerr"] == hbs[0]["txerr"], "no TX errors (TXFE timeout or SPI)", f"txerr {hbs[0]['txerr']} -> {hbs[-1]['txerr']}", fail="WARN")

    res.ok(not st.resets, "no board reset during the check", "; ".join(r[1][:80] for r in st.resets))
    res.ok(not st.fatals and not st.asserts, "no fatal or assert lines", "; ".join((st.fatals + st.asserts)[:3]))

    a, b = hbs[0], hbs[-1]
    traffic = (
        f"ackrx {a.get('ackrx')}->{b.get('ackrx')}, datarx {a.get('datarx')}->{b.get('datarx')}, "
        f"rounds ok {a.get('ok')}->{b.get('ok')} fail {a.get('fail')}->{b.get('fail')}, "
        f"rx frames {a.get('rx')}->{b.get('rx')} fcs bad {a.get('fcsbad')}->{b.get('fcsbad')}, rssi {b.get('rssi')} dBm"
    )
    if args.expect_traffic:
        good = all(isinstance(b.get(k), int) and b[k] > a[k] for k in ("ackrx", "datarx", "ok"))
        res.ok(good and b.get("fail") == a.get("fail"), "link traffic with the HackRF", traffic)
    else:
        res.add("INFO", "link traffic", traffic)


def gdb_snapshot():
    rc, out = sh(["tools/gdb.sh", "print/x at86_link_status"], timeout=60, env={"GDB_MAXLINES": "400"})
    m = re.search(r"\$\d+ = \{(.*)\}", out, re.S)
    if rc != 0 or not m:
        return None, out
    vals = {}
    for k, v in re.findall(r"(\w+) = (0x[0-9a-fA-F]+|-?\d+)", m.group(1)):
        vals[k] = int(v, 0)
    return vals, out


def check_gdb(res):
    rc, out = sh(["tools/jlink.sh", "status"])
    if "RUNNING" not in out or "NOT RUNNING" in out:
        res.add("FAIL", "GDB server running", "start it with: tools/jlink.sh start")
        return
    a, out_a = gdb_snapshot()
    if a is None:
        res.add("FAIL", "GDB: read at86_link_status", out_a.strip().splitlines()[-1] if out_a.strip() else "no output")
        return
    time.sleep(3)
    b, out_b = gdb_snapshot()
    if b is None:
        res.add("FAIL", "GDB: second read of at86_link_status", out_b.strip().splitlines()[-1] if out_b.strip() else "")
        return
    res.ok(a.get("magic") == STATUS_MAGIC, "GDB: status magic", f"0x{a.get('magic', 0):08x} (wrong value: ELF does not match the flashed image, or the test never started)")
    res.ok(b.get("polls", 0) > a.get("polls", 0), "GDB: poll counter grows", f"{a.get('polls')} -> {b.get('polls')}")
    res.ok(b.get("uptime_ms", 0) > a.get("uptime_ms", 0), "GDB: uptime grows", f"{a.get('uptime_ms')} -> {b.get('uptime_ms')} ms")
    res.ok(b.get("part_number") in VALID_PN, "GDB: part number", f"0x{b.get('part_number', 0):02x}")
    res.ok(b.get("phase") not in (None, 0, 7), "GDB: phase", PHASES.get(b.get("phase"), str(b.get("phase"))))


def follow(path, seconds, st, quiet):
    """Reads new lines from the log for `seconds`, giving each one the host time of arrival."""
    end = time.time() + seconds
    last_print = 0
    with open(path, "r", errors="replace") as f:
        f.seek(0, os.SEEK_END)
        partial = ""
        while time.time() < end:
            chunk = f.read()
            if not chunk:
                time.sleep(0.2)
                continue
            data = partial + chunk
            lines = data.split("\n")
            partial = lines.pop()
            now = time.time()
            for ln in lines:
                st.line(ln, now)
            hbs = st.complete_heartbeats()
            if not quiet and hbs and hbs[-1]["n"] != last_print:
                h = hbs[-1]
                last_print = h["n"]
                print(
                    f"  hb {h['n']:>5} t={h['t'] / 1000:8.1f}s {PHASES.get(h.get('ph'), '?'):<13} rnd={h.get('rnd')} "
                    f"st={RF_STATES.get(h['st'], h['st']):<6} tx={h['tx']} rxl={h['rxl']} rx={h.get('rx')} "
                    f"ackrx={h.get('ackrx')} datarx={h.get('datarx')} ok={h.get('ok')} fail={h.get('fail')}",
                    flush=True,
                )
        if partial:
            st.line(partial, time.time())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", default=DEFAULT_LOG, help="live RTT log written by tools/rtt.sh")
    ap.add_argument("--file", help="check a saved log offline instead of the live board")
    ap.add_argument("--duration", type=float, default=60, help="seconds to watch the live log")
    ap.add_argument("--gdb", action="store_true", help="also read at86_link_status over GDB (halts the core briefly)")
    ap.add_argument("--expect-traffic", action="store_true", help="the HackRF is running: require ACK and DATA traffic")
    ap.add_argument("--no-start", action="store_true", help="do not start tools/rtt.sh if it is not running")
    ap.add_argument("--quiet", action="store_true", help="do not print each heartbeat")
    args = ap.parse_args()

    res = Results()
    st = LogState()

    if args.file:
        with open(args.file, "r", errors="replace") as f:
            for ln in f:
                st.line(ln, 0.0)
        check_boot(res, st)
        hbs = st.complete_heartbeats()
        window = (hbs[-1]["t"] - hbs[0]["t"]) / 1000 if len(hbs) >= 2 else 0
        check_live(res, st, args, max(window, HEARTBEAT_S), live=False)
    else:
        if not ensure_rtt(res, args):
            return 1
        # Startup lines that are already in the log (the capture may have begun at boot)
        with open(args.log, "r", errors="replace") as f:
            for ln in f:
                st.line(ln, 0.0)
        boot_state = st
        check_boot(res, boot_state)
        live = LogState()
        live.saw_start = True  # a new start line during the watch means a reset
        print(f"watching {args.log} for {args.duration:.0f} s ...", flush=True)
        follow(args.log, args.duration, live, args.quiet)
        check_live(res, live, args, args.duration, live=True)
        if args.gdb:
            check_gdb(res)

    print(f"\nat86 selftest: {'PASS' if res.failed == 0 else f'FAIL ({res.failed} failed)'}")
    return 0 if res.failed == 0 else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
