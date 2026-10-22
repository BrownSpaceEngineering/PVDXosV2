#!/usr/bin/env python3
"""
HackRF side of the AT86RF215 MR-FSK link test (firmware: src/tests/test_at86_link.c, `make test TESTS="at86_link"`).

The PHY matches the firmware: 2-FSK, 50 ksym/s, modulation index 1.0 (deviation +-25 kHz), Gaussian BT 1.0,
8 octet preamble, 16-bit SFD (register value 0x7209, sent LSB first), IEEE 802.15.4g PHR, no whitening, no FEC,
2 byte FCS (CRC-16 ITU-T, as in IEEE 802.15.4).

Each round (mirror of the firmware):
  Phase A: listen for AT86 DATA, send ACK after it, then listen for repeats and ACK them again.
  Phase B: send our DATA, listen for the AT86 ACK, send again if none comes.

Every AT86 DATA gets an ACK, in any phase. New AT86 DATA during phase B means that the AT86 gave up waiting for our
DATA and started its next round, so phase A of our next round starts with that DATA.

One HackRF cannot transmit and receive at the same time, so the script opens the device for RX, closes it, opens it
for TX, and so on. Each switch takes about a second over USB. The logs show it: "rxready=" is the time until RX
streams after an open, and "turnaround=" is the time from a received frame to the end of our ACK. TX uses GNU Radio's digital.gfsk_mod. RX uses the GNU Radio channel filter and a numpy
demodulator that also handles the 802.15.4g framing. `--selftest` runs the same TX and RX code on generated bursts,
without hardware.

Usage:
  python3 scripts/hackrf_link.py --selftest              # offline check of the modulator, filter and demodulator
  python3 scripts/hackrf_link.py --check-device          # hackrf_info and one short RX capture
  python3 scripts/hackrf_link.py --listen                # RX only: print every frame (bring-up step 1)
  python3 scripts/hackrf_link.py --beacon                # TX only: send DATA every second (bring-up step 2)
  python3 scripts/hackrf_link.py                         # full exchange, forever (or --rounds N)
"""

import argparse
import queue
import struct
import subprocess
import sys
import time

import numpy as np

# ---------------------------------------------------------------- packet format (src/tests/at86_link_packet.h)

LINK_VERSION = 1
LINK_SSID_AT86 = 1
LINK_SSID_HACKRF = 2
LINK_MAX_PAYLOAD = 32
LINK_TYPE_DATA = 0
LINK_TYPE_ACK = 1
HEADER_FMT = "<B6sBIB"  # length, callsign, ver_ssid, seq, type
HEADER_SIZE = struct.calcsize(HEADER_FMT)  # 13
assert HEADER_SIZE == 13
TYPE_NAMES = {LINK_TYPE_DATA: "DATA", LINK_TYPE_ACK: "ACK"}


def build_packet(callsign: str, ptype: int, seq: int, payload: bytes = b"") -> bytes:
    payload = payload[:LINK_MAX_PAYLOAD]
    call = callsign.encode("ascii")[:6].ljust(6, b" ")
    length = HEADER_SIZE - 1 + len(payload)
    ver_ssid = (LINK_VERSION << 4) | LINK_SSID_HACKRF
    return struct.pack(HEADER_FMT, length, call, ver_ssid, seq & 0xFFFFFFFF, ptype) + payload


def parse_packet(psdu: bytes):
    """Returns a dict, or None with a reason string."""
    if len(psdu) < HEADER_SIZE:
        return None, "short"
    length, call, ver_ssid, seq, ptype = struct.unpack_from(HEADER_FMT, psdu)
    if length != len(psdu) - 1:
        return None, f"length field {length} != {len(psdu) - 1}"
    if (ver_ssid >> 4) != LINK_VERSION:
        return None, f"version {ver_ssid >> 4}"
    if ptype not in TYPE_NAMES:
        return None, f"type {ptype}"
    return {
        "type": ptype,
        "seq": seq,
        "ssid": ver_ssid & 0xF,
        "callsign": call.decode("ascii", errors="replace"),
        "payload": psdu[HEADER_SIZE:],
    }, ""


# ---------------------------------------------------------------- PHY

SYMBOL_RATE = 50e3
DEVIATION = 25e3  # h = 1.0
BT = 1.0
PREAMBLE_OCTETS = 8
DEV_RATE = 2e6  # HackRF sample rate
DECIM = 5
FS = DEV_RATE / DECIM  # 400 kS/s after the channel filter
SPS = int(FS / SYMBOL_RATE)  # 8
TX_SPS = int(DEV_RATE / SYMBOL_RATE)  # 40
LO_OFFSET = 250e3  # Tune away from the channel so the HackRF DC spike and LO leakage stay out of it
MAX_PSDU = 64  # Longest PSDU the demodulator waits for (our packets are at most 45 + 2)


def crc16_kermit(data: bytes) -> int:
    """CRC-16 ITU-T as used by IEEE 802.15.4: reflected poly 0x1021 (0x8408), init 0, no final XOR."""
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc


def crc16_x25(data: bytes) -> int:
    """Same polynomial with init 0xFFFF and final XOR 0xFFFF. Only used to report a mismatch during bring-up."""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc ^ 0xFFFF


def octets_to_bits_lsb(data: bytes) -> list:
    return [(b >> i) & 1 for b in data for i in range(8)]


def bits_to_octets_lsb(bits) -> bytes:
    out = bytearray()
    for i in range(0, len(bits) - 7, 8):
        v = 0
        for j in range(8):
            v |= int(bits[i + j]) << j
        out.append(v)
    return bytes(out)


def sfd_bits(sfd_reg: int) -> list:
    """FSKSFD0 register value -> air bits (the chip sends the 16-bit value LSB first)."""
    return [(sfd_reg >> i) & 1 for i in range(16)]


def phr_bits(psdu_len_with_fcs: int) -> list:
    """IEEE 802.15.4g PHR in air order: MS, R1, R0, FCS type (1 = 2 octet), DW, L10..L0 (MSB first)."""
    head = [0, 0, 0, 1, 0]
    return head + [(psdu_len_with_fcs >> (10 - i)) & 1 for i in range(11)]


def frame_bits(psdu: bytes, sfd_reg: int) -> list:
    crc = crc16_kermit(psdu)
    body = psdu + bytes([crc & 0xFF, crc >> 8])
    preamble = [0, 1] * (4 * PREAMBLE_OCTETS)
    return preamble + sfd_bits(sfd_reg) + phr_bits(len(body)) + octets_to_bits_lsb(body)


GFSK_FLUSH_BITS = [0, 1] * 4  # Sent after the FCS: the Gaussian filter delays the signal and would cut the last bits


def gfsk_modulate_raw(bits, sps: int = TX_SPS) -> np.ndarray:
    """Runs air-order bits through GNU Radio's digital.gfsk_mod. Unit amplitude, no frequency shift, no padding.

    do_unpack=False: the input is already one bit per item in air order. The default (True) unpacks each byte MSB
    first, but 802.15.4g sends the PSDU LSB first, so the default breaks the frame.
    The block maps 0 to -DEVIATION and 1 to +DEVIATION, like the AT86RF215.
    """
    from gnuradio import blocks, digital, gr

    mod = digital.gfsk_mod(
        samples_per_symbol=sps,
        sensitivity=2 * np.pi * DEVIATION / (sps * SYMBOL_RATE),  # phase step per sample for +-DEVIATION
        bt=BT,
        do_unpack=False,
    )
    tb = gr.top_block()
    src = blocks.vector_source_b([int(b) for b in bits], False)
    snk = blocks.vector_sink_c()
    tb.connect(src, mod, snk)
    tb.run()
    return np.array(snk.data(), dtype=np.complex64)


def modulate(bits, fs: float = DEV_RATE, shift: float = -LO_OFFSET, amp: float = 0.7, pad_s: float = 0.005) -> np.ndarray:
    """2-GFSK burst from GNU Radio's gfsk_mod, shifted by `shift` Hz (the device is tuned LO_OFFSET above the channel)."""
    sps = int(round(fs / SYMBOL_RATE))
    burst = gfsk_modulate_raw(list(bits) + GFSK_FLUSH_BITS, sps).astype(np.complex128)
    n = np.arange(len(burst))
    burst = amp * burst * np.exp(2j * np.pi * shift * n / fs)
    # Ramp the ends a little so the burst does not splatter. The ramp only covers the flush bits and the preamble
    ramp = min(len(burst) // 10, sps * 2)
    w = np.ones(len(burst))
    w[:ramp] = np.linspace(0, 1, ramp)
    w[-ramp:] = np.linspace(1, 0, ramp)
    pad = np.zeros(int(pad_s * fs), dtype=np.complex64)
    return np.concatenate([pad, (burst * w).astype(np.complex64), pad, pad, pad, pad])


class Demod:
    """Streaming 2-FSK demodulator on 400 kS/s channel samples. feed() returns decoded frames."""

    def __init__(self, sfd_reg: int, verbose: bool = False):
        self.sync = np.array([0, 1] * 4 + sfd_bits(sfd_reg), dtype=np.float64) * 2 - 1  # last preamble octet + SFD
        self.nsync = len(self.sync)
        self.verbose = verbose
        self.buf = np.zeros(0, dtype=np.complex64)
        self.base = 0  # absolute index of buf[0]
        self.done_until = 0  # absolute index; ignore sync hits before it
        self.keep = (self.nsync + 32 + 16 + 8 * MAX_PSDU + 64) * SPS
        self.stats = {"sync": 0, "phr_bad": 0, "crc_bad": 0, "crc_ok": 0}

    def feed(self, x: np.ndarray, flush: bool = False):
        self.buf = np.concatenate([self.buf, x.astype(np.complex64)])
        if len(self.buf) < self.keep * 2 and not flush:
            return []
        frames = self._process(flush)
        if not flush and len(self.buf) > self.keep:
            drop = len(self.buf) - self.keep
            self.buf = self.buf[drop:]
            self.base += drop
        return frames

    def _process(self, flush: bool):
        x = self.buf
        if len(x) < 4 * SPS:
            return []
        disc = np.angle(x[1:] * np.conj(x[:-1])) * FS / (2 * np.pi)
        fm = np.convolve(disc, np.ones(SPS) / SPS, mode="same")
        pwr = np.abs(x[1:]) ** 2

        hits = []
        for p in range(SPS):
            s = fm[p::SPS]
            if len(s) < self.nsync + 32:
                continue
            dc = np.convolve(s, np.ones(32) / 32, mode="same")
            hard = np.sign(s - dc)
            corr = np.correlate(hard, self.sync, mode="valid")
            for k in np.nonzero(np.abs(corr) >= self.nsync - 3)[0]:
                abs_idx = self.base + p + k * SPS
                if abs_idx < self.done_until:
                    continue
                soft = float(np.dot((s[k : k + self.nsync] - dc[k : k + self.nsync]), self.sync))
                hits.append((abs_idx, p, int(k), soft))
        if not hits:
            return []

        # One frame shows up at several sample phases and neighbouring positions: keep the strongest of each cluster
        hits.sort()
        clusters, cur = [], [hits[0]]
        for h in hits[1:]:
            if h[0] - cur[-1][0] <= 2 * SPS:
                cur.append(h)
            else:
                clusters.append(cur)
                cur = [h]
        clusters.append(cur)

        frames = []
        for c in clusters:
            abs_idx, p, k, soft = max(c, key=lambda h: abs(h[3]))
            if abs_idx < self.done_until:
                continue
            res = self._decode(fm, pwr, p, k, 1.0 if soft > 0 else -1.0, flush)
            if res is None:
                continue  # not enough samples yet, try again with the next chunk
            self.stats["sync"] += 1
            end_sym, frame = res
            self.done_until = self.base + p + end_sym * SPS
            if frame is not None:
                frames.append(frame)
        return frames

    def _decode(self, fm, pwr, p, k, pol, flush):
        s = fm[p::SPS]
        pre_start = max(0, k - 16)
        if k - pre_start < 8:
            dc = float(np.mean(s[k : k + 8]))
        else:
            dc = float(np.mean(s[pre_start:k + 8]))  # preamble symbols are balanced: their mean is the frequency offset
        k0 = k + self.nsync
        if len(s) < k0 + 16:
            return None if not flush else (k0, None)
        bits = ((s - dc) * pol > 0).astype(np.uint8)
        phr = bits[k0 : k0 + 16]
        length = 0
        for b in phr[5:]:
            length = (length << 1) | int(b)
        fcs_bit = int(phr[3])
        if length < 3 or length > MAX_PSDU:
            self.stats["phr_bad"] += 1
            if self.verbose:
                print(f"hrfdbg phr bad bits={''.join(map(str, phr))} len={length}", flush=True)
            return (k0 + 16, None)
        need = k0 + 16 + 8 * length
        if len(s) < need:
            return None if not flush else (k0 + 16, None)
        body = bits_to_octets_lsb(bits[k0 + 16 : need])
        psdu, fcs = body[:-2], body[-2:]
        got = fcs[0] | (fcs[1] << 8)
        crc_ok = got == crc16_kermit(psdu)
        a, b = (p + k * SPS), (p + need * SPS)
        power_db = 10 * np.log10(np.mean(pwr[a:b]) + 1e-12)
        frame = {
            "psdu": psdu,
            "crc_ok": crc_ok,
            "crc_x25": got == crc16_x25(psdu),
            "fcs_bit": fcs_bit,
            "foff": dc,
            "power_db": power_db,
            "inverted": pol < 0,
        }
        self.stats["crc_ok" if crc_ok else "crc_bad"] += 1
        return (need, frame)


# ---------------------------------------------------------------- GNU Radio plumbing


def channel_taps():
    from gnuradio.filter import firdes

    return firdes.low_pass(1.0, DEV_RATE, 75e3, 25e3)


def make_queue_sink(q):
    """Returns (native sink block, stop function). A Python gr.sync_block segfaults on Python 3.14 with GNU Radio 3.10.12,
    so a native file_descriptor_sink writes into a pipe and a reader thread fills the queue.

    The sink owns the write end of the pipe and closes it in its destructor. Do not close it here as well: the second
    close can hit an unrelated file that got the same descriptor number in the meantime."""
    import os
    import select
    import threading

    from gnuradio import blocks, gr

    rfd, wfd = os.pipe()
    sink = blocks.file_descriptor_sink(gr.sizeof_gr_complex, wfd)
    stop = threading.Event()

    def reader():
        pending = b""
        while not stop.is_set():
            ready, _, _ = select.select([rfd], [], [], 0.05)  # The timeout lets the thread see `stop`
            if not ready:
                continue
            data = os.read(rfd, 1 << 16)
            if not data:
                break
            pending += data
            n = len(pending) // 8 * 8
            if n:
                q.put(np.frombuffer(pending[:n], dtype=np.complex64).copy())
                pending = pending[n:]

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    def close():
        """Call after tb.stop() and tb.wait(), when the sink writes nothing more."""
        stop.set()
        t.join()
        os.close(rfd)

    return sink, close


def run_channel_filter_offline(samples: np.ndarray) -> np.ndarray:
    """Runs the same GNU Radio channel filter as the live RX path on a sample vector."""
    from gnuradio import blocks, filter, gr

    tb = gr.top_block()
    src = blocks.vector_source_c(samples.tolist(), False)
    xlat = filter.freq_xlating_fir_filter_ccc(DECIM, channel_taps(), -LO_OFFSET, DEV_RATE)
    snk = blocks.vector_sink_c()
    tb.connect(src, xlat, snk)
    tb.run()
    return np.array(snk.data(), dtype=np.complex64)


class Radio:
    def __init__(self, args):
        self.args = args
        self.demod = Demod(args.sfd, args.verbose)
        self.rx_ready_s = None  # Time from the start of the last listen() until the first samples arrived

    def _dev_args(self):
        a = "numchan=1 hackrf=0"
        if self.args.serial:
            a = f"numchan=1 hackrf={self.args.serial}"
        return a

    def tx(self, psdu: bytes) -> float:
        """Opens the device for TX, sends one frame and closes it. Returns the time that took, in seconds."""
        import osmosdr
        from gnuradio import blocks, gr

        t0 = time.monotonic()
        samples = modulate(frame_bits(psdu, self.args.sfd), shift=-LO_OFFSET)
        tb = gr.top_block()
        src = blocks.vector_source_c(samples.tolist(), False)
        snk = osmosdr.sink(args=self._dev_args())
        snk.set_sample_rate(DEV_RATE)
        snk.set_center_freq(self.args.freq + LO_OFFSET)
        snk.set_freq_corr(self.args.ppm)
        snk.set_gain(self.args.tx_amp)  # RF amp: 0 or 14 dB
        snk.set_if_gain(self.args.tx_vga)  # TX VGA: 0..47 dB
        snk.set_bandwidth(1.75e6)
        tb.connect(src, snk)
        tb.run()
        del tb, snk
        return time.monotonic() - t0

    def listen(self, timeout_s: float, want=None):
        """Receives for up to timeout_s. Calls want(frame) for each decoded frame; stops early when it returns True.
        Returns the frame that stopped it, or None. Each frame gets "t_rx", the host time when it was decoded."""
        import osmosdr
        from gnuradio import blocks, filter, gr

        t0 = time.monotonic()
        self.rx_ready_s = None
        q = queue.Queue()
        tb = gr.top_block()
        src = osmosdr.source(args=self._dev_args())
        src.set_sample_rate(DEV_RATE)
        src.set_center_freq(self.args.freq + LO_OFFSET)
        src.set_freq_corr(self.args.ppm)
        src.set_gain_mode(False)
        src.set_gain(self.args.rx_amp)  # RF amp: 0 or 14 dB
        src.set_if_gain(self.args.rx_lna)  # LNA: 0..40 dB, 8 dB steps
        src.set_bb_gain(self.args.rx_vga)  # VGA: 0..62 dB, 2 dB steps
        src.set_bandwidth(1.75e6)
        xlat = filter.freq_xlating_fir_filter_ccc(DECIM, channel_taps(), -LO_OFFSET, DEV_RATE)
        qsink, close_sink = make_queue_sink(q)
        tb.connect(src, xlat, qsink)
        if self.args.dump_iq:
            tb.connect(xlat, blocks.file_sink(gr.sizeof_gr_complex, self.args.dump_iq, True))
        self.demod = Demod(self.args.sfd, self.args.verbose)  # fresh stream: sample indexes restart
        tb.start()
        result = None
        deadline = time.monotonic() + timeout_s
        try:
            while time.monotonic() < deadline and result is None:
                try:
                    chunk = q.get(timeout=0.05)
                except queue.Empty:
                    continue
                if self.rx_ready_s is None:
                    self.rx_ready_s = time.monotonic() - t0
                for fr in self.demod.feed(chunk):
                    fr["t_rx"] = time.monotonic()
                    if want is None or want(fr):
                        result = fr
                        break
        finally:
            tb.stop()
            tb.wait()
            close_sink()
            del tb, src, xlat, qsink  # The sink closes the write end of its pipe here
        return result


# ---------------------------------------------------------------- link logic


class Link:
    def __init__(self, args):
        self.args = args
        self.radio = Radio(args)
        self.seq = 0
        self.last_at86_seq = None
        self.stats = dict(rx=0, crc_bad=0, rej=0, data_rx=0, dup=0, ack_tx=0, ack_rx=0, data_tx=0, ok=0, fail=0)

    def log(self, msg):
        print(f"{time.strftime('%H:%M:%S')} {msg}", flush=True)

    def _rx_ready(self):
        r = self.radio.rx_ready_s
        return "rxready=?" if r is None else f"rxready={r:.2f}s"

    def _check(self, fr):
        """Validates a frame. Returns the parsed packet from the AT86, or None."""
        self.stats["rx"] += 1
        info = f"len={len(fr['psdu']) + 2} foff={fr['foff']:+.0f}Hz pwr={fr['power_db']:.1f}dBFS {self._rx_ready()}"
        if fr["inverted"]:
            info += " INVERTED"
        if not fr["crc_ok"]:
            self.stats["crc_bad"] += 1
            hint = " (matches CRC-16/X25 instead: init/xorout differ)" if fr["crc_x25"] else ""
            self.log(f"hrfpkt rx CRC_BAD {info} psdu={fr['psdu'].hex()}{hint}")
            return None
        pkt, why = parse_packet(fr["psdu"])
        if pkt is None or pkt["ssid"] != LINK_SSID_AT86:
            self.stats["rej"] += 1
            self.log(f"hrfpkt rx REJECT {why or 'ssid ' + str(pkt['ssid'])} {info}")
            return None
        self.log(f"hrfpkt rx {TYPE_NAMES[pkt['type']]} seq={pkt['seq']} call={pkt['callsign']} {info} payload={pkt['payload']!r}")
        pkt["t_rx"] = fr["t_rx"]
        return pkt

    def send(self, ptype, seq, payload=b"", t_rx=None):
        """t_rx: host time of the frame that this packet answers. The log then shows the RX to TX turnaround."""
        psdu = build_packet(self.args.callsign, ptype, seq, payload)
        took = self.radio.tx(psdu)
        extra = f" tx={took:.2f}s"
        if t_rx is not None:
            extra += f" turnaround={time.monotonic() - t_rx:.2f}s"
        self.log(f"hrfpkt tx {TYPE_NAMES[ptype]} seq={seq} len={len(psdu) + 2}{extra}")

    def send_ack(self, pkt):
        self.send(LINK_TYPE_ACK, pkt["seq"], t_rx=pkt["t_rx"])
        self.stats["ack_tx"] += 1

    def is_new_data(self, pkt):
        return pkt["type"] == LINK_TYPE_DATA and pkt["seq"] != self.last_at86_seq

    def accept_data(self, pkt):
        """New AT86 DATA: count it, remember its sequence number and send the ACK."""
        self.stats["data_rx"] += 1
        self.last_at86_seq = pkt["seq"]
        self.send_ack(pkt)

    def listen_for(self, timeout_s, match):
        """Listens until match(pkt) is true or new AT86 DATA arrives, and returns that packet (None after timeout_s).
        Repeated AT86 DATA that we already ACKed gets a new ACK on the way. New DATA goes back to the caller, which
        must accept it (accept_data), so that every AT86 DATA gets an ACK."""
        deadline = time.monotonic() + timeout_s
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                return None
            hit = {}

            def want(fr):
                pkt = self._check(fr)
                if pkt is None:
                    return False
                hit["pkt"] = pkt
                return True

            if self.radio.listen(left, want) is None:
                return None
            pkt = hit["pkt"]
            if match(pkt) or self.is_new_data(pkt):
                return pkt
            if pkt["type"] == LINK_TYPE_DATA:  # The DATA we accepted last: the AT86 did not hear our ACK
                self.stats["dup"] += 1
                self.send_ack(pkt)

    def dup_window(self):
        """Stays in RX after our ACK. listen_for() ACKs repeats of the same AT86 DATA again."""
        deadline = time.monotonic() + self.args.dup_window
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                return
            pkt = self.listen_for(left, lambda p: False)
            if pkt is None:
                return
            self.accept_data(pkt)  # New AT86 DATA is not expected here, but every DATA gets an ACK

    def phase_a(self, pkt=None):
        """AT86 DATA, then our ACK. `pkt` is new AT86 DATA that already arrived during phase B of the last round."""
        if pkt is None:
            pkt = self.listen_for(self.args.data_wait, lambda p: False)  # Returns on new AT86 DATA
            if pkt is None:
                return False
        self.accept_data(pkt)
        self.dup_window()
        return True

    def phase_b(self):
        """Our DATA, then the AT86 ACK. Returns (ok, pkt). pkt is new AT86 DATA that came instead of the ACK: the AT86
        stopped waiting for our DATA and started its next round, so our next round starts with that DATA."""
        payload = f"HACKRF round {self.seq}".encode()
        for attempt in range(1, self.args.tries + 1):
            self.send(LINK_TYPE_DATA, self.seq, payload)
            self.stats["data_tx"] += 1
            pkt = self.listen_for(self.args.ack_timeout, lambda p: p["type"] == LINK_TYPE_ACK and p["seq"] == self.seq)
            if pkt is None:
                self.log(f"hrfpkt no ACK for seq={self.seq} try={attempt} {self._rx_ready()}")
                continue
            if pkt["type"] == LINK_TYPE_ACK:
                self.stats["ack_rx"] += 1
                return True, None
            self.log(f"hrfpkt new AT86 DATA seq={pkt['seq']} in phase B: the AT86 started its next round")
            return False, pkt
        return False, None

    def run(self, rounds):
        n = 0
        pending = None  # New AT86 DATA from phase B, for phase A of the next round
        while rounds == 0 or n < rounds:
            a = self.phase_a(pending)
            pending = None
            b = False
            if a:
                b, pending = self.phase_b()
            self.stats["ok" if a and b else "fail"] += 1
            self.log(f"hrfround n={n} a={'ok' if a else 'FAIL'} b={'ok' if b else 'FAIL'} " + self._stats_str())
            self.seq += 1
            n += 1

    def _stats_str(self):
        return " ".join(f"{k}={v}" for k, v in self.stats.items())


# ---------------------------------------------------------------- modes


def selftest(args) -> int:
    """Offline loopback: modulate packets, add offset, noise and delay, run the live RX filter and demodulator."""
    rng = np.random.default_rng(1)
    failures = 0

    def check(name, ok, detail=""):
        nonlocal failures
        print(f"{'PASS' if ok else 'FAIL'}  {name}{('  ' + detail) if detail else ''}")
        if not ok:
            failures += 1

    # CRC reference: CRC-16/KERMIT check value for "123456789" is 0x2189
    check("crc16 kermit check value", crc16_kermit(b"123456789") == 0x2189, f"0x{crc16_kermit(b'123456789'):04x}")
    check("sfd air bits from register 0x7209", "".join(map(str, sfd_bits(0x7209))) == "1001000001001110")
    check("phr for 15 octets", phr_bits(15) == [0, 0, 0, 1, 0] + [0] * 7 + [1, 1, 1, 1])

    # gfsk_mod: steady runs of 1s and 0s must sit at +DEVIATION and -DEVIATION
    run = [1] * 40 + [0] * 40
    y = gfsk_modulate_raw(run).astype(np.complex128)
    f = np.angle(y[1:] * np.conj(y[:-1])) * DEV_RATE / (2 * np.pi)
    mid1 = np.median(f[10 * TX_SPS : 30 * TX_SPS])
    mid0 = np.median(f[50 * TX_SPS : 70 * TX_SPS])
    check(
        "gfsk_mod deviation and polarity",
        abs(mid1 - DEVIATION) < 0.02 * DEVIATION and abs(mid0 + DEVIATION) < 0.02 * DEVIATION,
        f"1 -> {mid1:+.0f} Hz, 0 -> {mid0:+.0f} Hz (want +-{DEVIATION:.0f})",
    )

    # gfsk_mod: every frame bit comes out, in order, with nothing cut at the end
    ref = frame_bits(build_packet(args.callsign, LINK_TYPE_DATA, 1, bytes(range(32))), args.sfd)
    y = gfsk_modulate_raw(ref + GFSK_FLUSH_BITS).astype(np.complex128)
    f = np.angle(y[1:] * np.conj(y[:-1]))
    best = (0, 0)
    for delay in range(0, 4 * TX_SPS):
        idx = delay + TX_SPS // 2 + np.arange(len(ref)) * TX_SPS
        if idx[-1] >= len(f):
            break
        match = int(np.sum((f[idx] > 0).astype(int) == np.array(ref)))
        best = max(best, (match, delay))
    check("gfsk_mod keeps every frame bit", best[0] == len(ref), f"{best[0]}/{len(ref)} bits at delay {best[1]} samples")

    pkts = [
        build_packet(args.callsign, LINK_TYPE_DATA, 7, b"PVDX AT86 round 7"),
        build_packet(args.callsign, LINK_TYPE_ACK, 7),
        build_packet(args.callsign, LINK_TYPE_DATA, 0xDEADBEEF, bytes(range(32))),
    ]
    for foff, snr_db in [(0, 30), (3e3, 20), (-8e3, 15)]:
        pieces = [np.zeros(int(rng.integers(1000, 20000)), dtype=np.complex64)]
        for p in pkts:
            pieces.append(modulate(frame_bits(p, args.sfd)))
            pieces.append(np.zeros(int(rng.integers(5000, 30000)), dtype=np.complex64))
        x = np.concatenate(pieces)
        n = np.arange(len(x))
        x = x * np.exp(2j * np.pi * foff * n / DEV_RATE)
        sig_p = 0.7**2
        noise = np.sqrt(sig_p / (10 ** (snr_db / 10)) / 2) * (rng.standard_normal(len(x)) + 1j * rng.standard_normal(len(x)))
        # SNR is set per DEV_RATE bandwidth; the channel filter adds about 10*log10(5) = 7 dB
        x = (x + noise).astype(np.complex64)
        y = run_channel_filter_offline(x)
        d = Demod(args.sfd, args.verbose)
        got = []
        for i in range(0, len(y), 4096):  # same chunking idea as the live path
            got += d.feed(y[i : i + 4096])
        got += d.feed(np.zeros(0, dtype=np.complex64), flush=True)
        ok_psdus = [f["psdu"] for f in got if f["crc_ok"]]
        detail = f"decoded {len(ok_psdus)}/{len(pkts)}"
        if got:
            detail += f", foff est {np.mean([f['foff'] for f in got]):+.0f} Hz"
        check(f"gfsk_mod -> channel filter -> demod, foff={foff:+.0f}Hz snr={snr_db}dB", ok_psdus == pkts, detail)

    # Pure noise must not produce packets with a good CRC
    noise = (0.05 * (rng.standard_normal(2_000_000) + 1j * rng.standard_normal(2_000_000))).astype(np.complex64)
    d = Demod(args.sfd)
    y = run_channel_filter_offline(noise)
    got = d.feed(y, flush=True)
    check("no false packets in 1 s of noise", not any(f["crc_ok"] for f in got), f"{len(got)} sync hits")

    print(f"selftest: {'PASS' if failures == 0 else f'FAIL ({failures})'}")
    return 0 if failures == 0 else 1


def check_device(args) -> int:
    try:
        out = subprocess.run(["hackrf_info"], capture_output=True, text=True, timeout=10)
    except FileNotFoundError:
        print("FAIL  hackrf_info not installed")
        return 1
    found = "Serial number" in out.stdout
    print(f"{'PASS' if found else 'FAIL'}  hackrf_info sees a device")
    if not found:
        print(out.stdout + out.stderr)
        return 1
    for line in out.stdout.splitlines():
        if "Serial number" in line or "Firmware Version" in line:
            print("      " + line.strip())
    radio = Radio(args)
    t0 = time.monotonic()
    radio.listen(1.0)
    print(f"PASS  1 s RX capture through GNU Radio ({time.monotonic() - t0:.2f} s including device open)")
    return 0


def listen_mode(args) -> int:
    link = Link(args)
    link.log(f"hrflisten freq={args.freq:.0f} sfd=0x{args.sfd:04x}")
    while True:
        link.radio.listen(3600, lambda fr: link._check(fr) is not None and False)


def beacon_mode(args) -> int:
    link = Link(args)
    seq = 0
    while args.rounds == 0 or seq < args.rounds:
        link.send(LINK_TYPE_DATA, seq, f"HACKRF beacon {seq}".encode())
        seq += 1
        time.sleep(args.beacon_period)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--freq", type=float, default=437.765e6, help="channel center frequency in Hz (LINK_FREQ_HZ)")
    ap.add_argument("--sfd", type=lambda s: int(s, 0), default=0x7209, help="FSKSFD0 register value (firmware logs sfd0=)")
    ap.add_argument("--callsign", default="NOCALL", help="6 characters, same as LINK_CALLSIGN")
    ap.add_argument("--ppm", type=float, default=0.0, help="HackRF frequency correction")
    ap.add_argument("--serial", default="", help="HackRF serial number if several are connected")
    ap.add_argument("--rx-amp", type=int, default=0, choices=[0, 14])
    ap.add_argument("--rx-lna", type=int, default=24)
    ap.add_argument("--rx-vga", type=int, default=20)
    ap.add_argument("--tx-amp", type=int, default=0, choices=[0, 14])
    ap.add_argument("--tx-vga", type=int, default=20)
    ap.add_argument("--rounds", type=int, default=0, help="0 = forever")
    ap.add_argument("--tries", type=int, default=10)
    ap.add_argument("--ack-timeout", type=float, default=6.0, help="listen time for the AT86 ACK, s (AT86 waits LINK_ACK_DELAY_MS)")
    ap.add_argument("--dup-window", type=float, default=15.0, help="RX time after our ACK, s. Longer than the AT86 retry period")
    ap.add_argument("--data-wait", type=float, default=150.0, help="listen time for AT86 DATA, s. Longer than LINK_DATA_WAIT_MS")
    ap.add_argument("--beacon-period", type=float, default=1.0)
    ap.add_argument("--dump-iq", default="", help="also write the 400 kS/s channel samples (complex64) to this file")
    ap.add_argument("--verbose", action="store_true")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--selftest", action="store_true", help="offline loopback test, no hardware")
    mode.add_argument("--check-device", action="store_true", help="check that the HackRF answers and streams")
    mode.add_argument("--listen", action="store_true", help="RX only, print every frame")
    mode.add_argument("--beacon", action="store_true", help="TX only, send DATA every --beacon-period seconds")
    args = ap.parse_args()

    if len(args.callsign) > 6:
        ap.error("--callsign has at most 6 characters")
    if args.selftest:
        return selftest(args)
    if args.check_device:
        return check_device(args)
    if args.listen:
        return listen_mode(args)
    if args.beacon:
        return beacon_mode(args)
    Link(args).run(args.rounds)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
