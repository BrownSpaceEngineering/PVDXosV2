#!/usr/bin/env bash
# Capture the firmware log (RTT up-channel 1) to a file using the repo's own
# scripts/rtt_logs.py. Runs in the background so agents never block on it.
# Usage: tools/rtt.sh start|stop|status|tail [n]
# Note: RTT channel 0 is the PVDXos shell (telnet :19021); this doesn't touch it.
set -u
cd "$(dirname "$0")/.." || exit 1
PIDF=/tmp/pvdxos-rtt.pid
OUT=/tmp/pvdxos-rtt.log

running() { [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; }

case "${1:-}" in
start)
  if running; then echo "already capturing (pid $(cat "$PIDF")) -> $OUT"; exit 0; fi
  python3 -c 'import pylink' 2>/dev/null || { echo "missing python module: pip install pylink-square" >&2; exit 1; }
  : > "$OUT"
  nohup python3 -u scripts/rtt_logs.py > "$OUT" 2>&1 &
  echo $! > "$PIDF"
  sleep 2
  if ! running; then
    echo "rtt_logs.py exited early. Output:" >&2
    tail -n 20 "$OUT" >&2
    rm -f "$PIDF"; exit 1
  fi
  echo "capturing RTT channel 1 -> $OUT (rtt_logs.py also writes logs/<timestamp>.log)"
  ;;
stop)
  if running; then kill "$(cat "$PIDF")" 2>/dev/null; fi
  rm -f "$PIDF"; echo "stopped"
  ;;
status)
  if running; then echo "CAPTURING pid $(cat "$PIDF"), $(wc -c < "$OUT") bytes in $OUT"; else echo "NOT CAPTURING"; fi
  ;;
tail)
  tail -n "${2:-100}" "$OUT" 2>/dev/null || echo "no RTT log yet (run: tools/rtt.sh start)"
  ;;
*)
  echo "usage: $0 start|stop|status|tail [n]"; exit 1 ;;
esac
